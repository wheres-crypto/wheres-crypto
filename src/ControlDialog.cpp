#include <Windows.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QHeaderView>
#include <sstream>

#include <ida.hpp>
#include <funcs.hpp>
#include <idp.hpp>
#include <diskio.hpp>

#include "common.hpp"
#include "types.hpp"
#include "Arm.hpp"
#include "DFGraph.hpp"
#include "Broker.hpp"
#include "Predicate.hpp"
#include "DFGDisplay.hpp"
#include "SignatureParser.hpp"
#include "SignatureEvaluator.hpp"
#include "ThreadPool.hpp"
#include "ControlDialog.hpp"
#include "PathOracle.hpp"
#include "SlidingStackedWidget.hpp"
#include "AnalysisResult.hpp"
#include "BlockPermutationEvaluator.hpp"

void CoordinatorThread::run() {
	std::list<unsigned long>::const_iterator itF;
	ThreadPool oPool(ThreadPool::create());
	ThreadTaskResult oResult;
	std::unordered_map<Broker, AnalysisResult> aResultTracker;
	std::unordered_map<Broker, AnalysisResult>::iterator itT;
	DWORD dwStartTime = GetTickCount();
	bool bScheduled;

	wc_debug("[+] Analysis started\n");
	wc_debug("[*] thread pool consists of %d threads\n", oPool->dwNumThreads);

	do {
		bScheduled = ScheduleNextFunction(oPool);
	} while (bScheduled);

	for (;;) {
		oResult = oPool->WaitForResult();
		if (oResult != nullptr) {
			CodeBroker oCodeGraph;
			AbstractEvaluationResult oEvaluationResult;
			AnalysisResult oAnalysisResult;
			std::list<SignatureDefinition>::iterator it;

			switch (oResult->dwType) {
			case THREAD_RESULT_TYPE_ANALYSIS_ERROR: {
				break;
			}
			case THREAD_RESULT_TYPE_CODE_GRAPH: {
				oCodeGraph = (CodeBrokerImpl*)oResult.lpNode;
				oAnalysisResult = AnalysisResult::create(oCodeGraph->toGeneric());
				wc_debug("[+] yielded a new code graph (%d nodes)\n", oCodeGraph->oGraph->size());
				for (it = aSignatureList.begin(); it != aSignatureList.end(); it++) {
					AbstractEvaluator oEvaluator = AbstractEvaluatorImpl::ScheduleEvaluate<SignatureEvaluator>(
						oPool,
						oCodeGraph->toGeneric(),
						PathOracleImpl::MaxEvaluationTime(),
						*it
					);
					/* init the result to null */
					oAnalysisResult->SetNull(oEvaluator);
				}
				AbstractEvaluator oBlockPermutation = AbstractEvaluatorImpl::ScheduleEvaluate<BlockPermutationEvaluator>(
					oPool,
					oCodeGraph->toGeneric(),
					PathOracleImpl::MaxEvaluationTime()
				);
				oAnalysisResult->SetNull(oBlockPermutation);

				/* for each code graph we have an analysis result, which in turn consists of multiple evaluation results */
				aResultTracker.insert(std::pair<Broker, AnalysisResult>(oCodeGraph->toGeneric(), oAnalysisResult));

				if (oAnalysisResult->AllResultsSet()) {
					/* this if statement is only true when the set of evaluators is empty */
					goto _all_results_set;
				}
				break;
			}
			case THREAD_RESULT_TYPE_EVALUATION_RESULT:
				oEvaluationResult = (AbstractEvaluationResultImpl*)oResult.lpNode;
				itT = aResultTracker.find(oEvaluationResult->oCodeGraph);
				if (itT != aResultTracker.end()) { /* should always find an entry */
					oAnalysisResult = itT->second;
					oAnalysisResult->SetResult(oEvaluationResult);

					if (oAnalysisResult->AllResultsSet()) {
_all_results_set:
						aResultTracker.erase(oEvaluationResult->oCodeGraph);
						emit ResultReady(oAnalysisResult);
					}
				}
				break;
			}
			ScheduleNextFunction(oPool);
		} else {
			wc_debug("[-] end of result report\n");
			break;
		}
	}

	wc_debug("[+] Analysis finished. Total running time was %fs\n", (double)(GetTickCount() - dwStartTime) / 1000);
	emit CoordinatorFinished();
}

bool CoordinatorThread::ScheduleNextFunction(ThreadPool &oPool) {
	std::list<unsigned long>::iterator itF = aFunctionList.begin();
	if (itF != aFunctionList.end()) {
		Processor oProcessor(Processor::typecast(Arm::create()));
		bool bScheduled = CodeBrokerImpl::ScheduleBuild(oProcessor, oPool, *itF, nullptr, true);
		if (bScheduled) {
			aFunctionList.erase(itF);
			emit NextFunction();
		}
		return bScheduled;
	}
	return false;
}

void ControlDialog::SetupFunctionSelector() {
	lpFunctionList = new FunctionList();
	lpFunctionSelector = new QWidget();
	QGridLayout *lpLayout = new QGridLayout(lpFunctionSelector);
	//lpLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *lpHeader = new QLabel("Select functions to analyze");
	QFont mFont = lpHeader->font();
	mFont.setWeight(QFont::ExtraLight);
	lpHeader->setFont(mFont);
	QLabel *lpHeaderImage = new QLabel();
	lpHeader->setStyleSheet("QLabel { font-size: 20pt; }");
	lpHeaderImage->setPixmap(QPixmap(":/images/wherescrypto.png").scaled(84, 125, Qt::KeepAspectRatio));
	lpHeaderImage->setStyleSheet("margin: 0 10px;");

	lpLayout->addWidget(lpHeaderImage, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
	lpLayout->addWidget(lpHeader, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
	lpLayout->addWidget(lpFunctionList, 1, 0, 1, 2);
	lpLayout->setRowStretch(1, 1);
	lpLayout->setColumnStretch(1, 1);
}

void ControlDialog::HandleResult(bool bBatchRun, AnalysisResult oResult) {
	AnalysisResultImpl::iterator it;
	std::stringstream szSignaturesFound;
	AnalysisResult oItem = bBatchRun ? nullptr : oResult;

	QTableWidgetItem *lpFunction = new result_item_t(QIcon(":/images/function.png"), oResult->oCodeGraph->toCodeGraph()->szFunctionName.c_str(), 0, oItem);
	QTableWidgetItem *lpPredicate = new result_item_t(oResult->oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(), 0, oItem);
	QTableWidgetItem* lpSize = new result_item_t(std::to_string(oResult->oCodeGraph->oGraph->size()).c_str(), oResult->oCodeGraph->oGraph->size(), oItem);
	for (it = oResult->begin(); it != oResult->end(); it++) {
		if (it->second->eStatus == EVALUATION_RESULT_MATCH_FOUND) {
			if (szSignaturesFound.tellp() != std::streampos(0)) {
				szSignaturesFound << ", ";
			}
			szSignaturesFound << it->second->Label();
		}
	}
	QTableWidgetItem* lpEvalutationResult = new result_item_t(szSignaturesFound.str().c_str(), 0, oItem);

	int dwNumRows = lpResultsTable->rowCount();
	lpResultsTable->setRowCount(dwNumRows + 1);
	lpResultsTable->setItem(dwNumRows, 0, lpFunction);
	lpResultsTable->setItem(dwNumRows, 1, lpPredicate);
	lpResultsTable->setItem(dwNumRows, 2, lpSize);
	lpResultsTable->setItem(dwNumRows, 3, lpEvalutationResult);
	lpFunction->setFlags(lpFunction->flags() & ~Qt::ItemIsEditable);
	lpPredicate->setFlags(lpPredicate->flags() & ~Qt::ItemIsEditable);
	lpSize->setFlags(lpSize->flags() & ~Qt::ItemIsEditable);
	lpEvalutationResult->setFlags(lpEvalutationResult->flags() & ~Qt::ItemIsEditable);

	wc_debug("[+] Result for %s (%.1500s) #nodes=%lu : %s\n",
		oResult->oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
		oResult->oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
		oResult->oCodeGraph->oGraph->size(),
		szSignaturesFound.str().c_str()
	);
}

void ControlDialog::HandleResultNormal(AnalysisResult oResult) {
	return HandleResult(false, oResult);
}

void ControlDialog::HandleResultBatch(AnalysisResult oResult) {
	return HandleResult(true, oResult);
}

void ControlDialog::NextFunction() {
	dwNumFunctionsLeft--;
	lpProgressBar->setValue(dwNumFunctions - dwNumFunctionsLeft);
}
void ControlDialog::CoordinatorFinished() {
	lpProgressHeader->setText("Analysis complete");
}

void ControlDialog::StartAnalysis(bool bBatchRun) {
	lpNextButton->setEnabled(false);
	lpBatchButton->setEnabled(false);
	emit slideInIdx(1);

	lpCoordinatorThread = new CoordinatorThread;

	connect(lpCoordinatorThread, SIGNAL(ResultReady(AnalysisResult)), this, bBatchRun ?
		SLOT(HandleResultBatch(AnalysisResult)) :
		SLOT(HandleResultNormal(AnalysisResult))
	);
	connect(lpCoordinatorThread, SIGNAL(CoordinatorFinished()), this, SLOT(CoordinatorFinished()));
	connect(lpCoordinatorThread, SIGNAL(NextFunction()), this, SLOT(NextFunction()));
	connect(lpCoordinatorThread, SIGNAL(finished()), lpCoordinatorThread, SLOT(deleteLater()));

	lpCoordinatorThread->aFunctionList = lpFunctionList->GetSelection();
	lpCoordinatorThread->aSignatureList = aSignatureList;

	dwNumFunctions = dwNumFunctionsLeft = lpCoordinatorThread->aFunctionList.size();

	lpProgressBar->setRange(0, dwNumFunctions);
	lpProgressHeader->setText("Analysis in progress...");
	lpCoordinatorThread->start();
}

void ControlDialog::StartAnalysis() {
	return StartAnalysis(false);
}

void ControlDialog::StartBatchRun() {
	return StartAnalysis(true);

}

void ControlDialog::SetupProgressView() {
	lpProgressView = new QWidget();
	QGridLayout *lpLayout = new QGridLayout(lpProgressView);
	lpProgressHeader = new QLabel();
	QFont mFont = lpProgressHeader->font();
	mFont.setWeight(QFont::ExtraLight);
	lpProgressHeader->setFont(mFont);
	QLabel* lpHeaderImage = new QLabel();
	lpProgressHeader->setStyleSheet("QLabel { font-size: 20pt; }");
	lpHeaderImage->setPixmap(QPixmap(":/images/wherescrypto.png").scaled(84, 125, Qt::KeepAspectRatio));
	lpHeaderImage->setStyleSheet("margin: 0 10px;");

	QStringList aColumns;
	lpResultsTable = new QTableWidget();
	lpResultsTable->setColumnCount(4);
	lpResultsTable->setShowGrid(false);
	lpResultsTable->verticalHeader()->hide();
	lpResultsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	lpResultsTable->verticalHeader()->setDefaultSectionSize(20);
	aColumns.push_back("Function name");
	aColumns.push_back("Predicate");
	aColumns.push_back("# nodes");
	aColumns.push_back("Patterns found");
	lpResultsTable->setHorizontalHeaderLabels(aColumns);
	lpResultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	lpResultsTable->setStyleSheet(" \
		QTableWidget: { border-collapse: collapse; }; \
		QTableWidgetItem: { padding: 0px; margin: 0px; }; \
	");
	lpResultsTable->setSortingEnabled(true);

	lpProgressBar = new QProgressBar();

	lpLayout->addWidget(lpHeaderImage, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
	lpLayout->addWidget(lpProgressHeader, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
	lpLayout->addWidget(lpResultsTable, 1, 0, 1, 2);
	lpLayout->addWidget(lpProgressBar, 2, 0, 1, 2);
	lpLayout->setRowStretch(1, 1);
	lpLayout->setColumnStretch(1, 1);

	lpProgressBar->setRange(0, 0);
	connect(lpResultsTable, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(DisplayResult(const QModelIndex&)));
}

void ControlDialog::DisplayResult(const QModelIndex& index) {
	int dwRowNo = index.row();
	result_item_t *lpRow = (result_item_t *)lpResultsTable->item(dwRowNo, 0);
	AnalysisResultImpl::iterator it;
	AbstractEvaluationResult oEvaluationResult(nullptr);

	if (lpRow->oAnalysisResult != nullptr) {
		for (it = lpRow->oAnalysisResult->begin(); it != lpRow->oAnalysisResult->end(); it++) {
			if (it->second->eStatus == EVALUATION_RESULT_MATCH_FOUND) {
				oEvaluationResult = it->second;
				break;
			}
		}
		std::stringstream ssTitle;
		ssTitle << "DFG of ";
		ssTitle << lpRow->oAnalysisResult->oCodeGraph->toCodeGraph()->szFunctionName;
		if (!lpRow->oAnalysisResult->oCodeGraph->toCodeGraph()->oStatePredicate->IsEmpty()) {
			ssTitle << " (";
			ssTitle << lpRow->oAnalysisResult->oCodeGraph->toCodeGraph()->oStatePredicate->expression(1);
			ssTitle << ")";
		}
		DFGDisplay oDisplay(DFGDisplay::create(
			ssTitle.str(),
			lpRow->oAnalysisResult->oCodeGraph->oGraph,
			oEvaluationResult
		));
		oDisplay->Display();
	}
}

// "QApplication::activeWindow()" gets the IDA parent QWidget
ControlDialog::ControlDialog()
	: QDialog(QApplication::activeWindow()) {
	// Initialize the dialog
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("Dialog"));
	resize(800, 600);
	lpLayout = new QGridLayout(this);
	lpNextButton = new QPushButton(QIcon(":/images/wherescrypto.png"), "Analyze");
	lpBatchButton = new QPushButton(QIcon(":/images/wherescrypto.png"), "Batch run");
	lpPrevButton = new QPushButton("Previous");
	lpCancelButton = new QPushButton("Cancel");
	//lpSplitter = new QSplitter(Qt::Horizontal);
	QStatusBar* lpStatusBar = new QStatusBar();
	lpSlider = new SlidingStackedWidget(this);
	//lpLayout->addWidget(lpSplitter, 0, 0);
	lpLayout->addWidget(lpSlider, 0, 0, 1, 5);
	lpLayout->addWidget(lpBatchButton, 1, 1, 1, 1, Qt::AlignVCenter | Qt::AlignLeft);
	lpLayout->addWidget(lpCancelButton, 1, 2, 1, 1, Qt::AlignVCenter | Qt::AlignRight);
	lpLayout->addWidget(lpPrevButton, 1, 3, 1, 1, Qt::AlignVCenter | Qt::AlignRight);
	lpLayout->addWidget(lpNextButton, 1, 4, 1, 1, Qt::AlignVCenter | Qt::AlignRight);
	lpLayout->addWidget(lpStatusBar, 2, 0, 1, 5);
	lpLayout->setRowStretch(0, 1);
	lpLayout->setColumnStretch(1, 1);
	lpLayout->setColumnMinimumWidth(0, 1);
	lpLayout->setContentsMargins(0, 0, 0, 0);
	//lpSplitter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

	SetupFunctionSelector();
	SetupProgressView();

	lpSlider->addWidget(lpFunctionSelector);
	lpSlider->addWidget(lpProgressView);
	lpSlider->setSpeed(500);

	setWindowTitle("Where's crypto?");
	setWindowIcon(QIcon(":/images/wherescrypto.png"));

	connect(lpBatchButton, SIGNAL(released()), this, SLOT(StartBatchRun()));
	connect(lpNextButton, SIGNAL(released()), this, SLOT(StartAnalysis()));
	connect(lpPrevButton, SIGNAL(released()), lpSlider, SLOT(slideInPrev()));
	connect(lpCancelButton, SIGNAL(released()), this, SLOT(reject()));
	connect(this, SIGNAL(slideInIdx(int)), lpSlider, SLOT(slideInIdx(int)));

	// Hide the help caption button
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	qRegisterMetaType<AnalysisResult>("AnalysisResult");
	ConstructSignatures();
	PathOracleImpl::Initialize();
	lpStatusBar->showMessage("Ready");
}

void ControlDialog::ConstructSignatures() {
	std::string szSpecification;
	struct stat stSpecFile;
	size_t dwNumBytesRead;
	int hSpecFile;
	SignatureBroker oBroker;

	const char* szPluginDir = idadir(PLG_SUBDIR);
	std::string szSearchPath = std::string(szPluginDir) +  "\\signatures\\*.*";
	WIN32_FIND_DATA stFindData;
	HANDLE hFind = ::FindFirstFile(szSearchPath.c_str(), &stFindData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			// read all (real) files in current folder
			// , delete '!' read other 2 default folder . and ..
			if (!(stFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				hSpecFile = ::open((std::string(szPluginDir) + "\\signatures\\" + stFindData.cFileName).c_str(), O_RDONLY | O_BINARY);
				::fstat(hSpecFile, &stSpecFile);

				szSpecification.resize(stSpecFile.st_size);
				dwNumBytesRead = read(hSpecFile, (char*)szSpecification.data(), stSpecFile.st_size);
				::close(hSpecFile);
				SignatureParser oParser(SignatureParser::create());
				SignatureDefinition oSignatureDefinition;
				if (oParser->Parse(&oSignatureDefinition, szSpecification, stFindData.cFileName) == PARSER_STATUS_OK) {
					wc_debug("[+] Successfully parsed signature (id=%s)\n", oSignatureDefinition->szIdentifier.c_str());
					aSignatureList.push_back(oSignatureDefinition);
				}
			}
		} while (::FindNextFile(hFind, &stFindData));
		::FindClose(hFind);
	}
}