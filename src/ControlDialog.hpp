#pragma once

#include <unordered_map>
#include <typeindex>

#include <QtCore/QThread>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QLabel>

#include "types.hpp"
#include "Broker.hpp"
#include "DFGraph.hpp"
#include "FunctionList.hpp"
#include "SlidingStackedWidget.hpp"
#include "SignatureEvaluator.hpp"
#include "AnalysisResult.hpp"

class CoordinatorThread: public QThread {
	Q_OBJECT
public:
	std::list<unsigned long> aFunctionList;
	std::list<SignatureDefinition> aSignatureList;
	void run();

private:
	bool ScheduleNextFunction(ThreadPool &oPool);

signals:
	void ResultReady(AnalysisResult oResult);
	void CoordinatorFinished();
	void NextFunction();
};

class result_item_t : public QTableWidgetItem {
public:
	result_item_t(const QIcon& icon, const QString& msg, int dwWeight, const AnalysisResult& oAnalysisResult)
		: QTableWidgetItem(icon, msg), dwWeight(dwWeight), oAnalysisResult(oAnalysisResult) { }

	result_item_t(const QString& msg, int dwWeight, const AnalysisResult& oAnalysisResult)
		: QTableWidgetItem(msg), dwWeight(dwWeight), oAnalysisResult(oAnalysisResult) { }

	inline bool operator < (QTableWidgetItem const & o) const {
		if (std::type_index(typeid(*this)) == std::type_index(typeid(o))) {
			if (dwWeight != ((result_item_t &)o).dwWeight) {
				return dwWeight < ((result_item_t &)o).dwWeight;
			}
		}
		return QTableWidgetItem::operator<(o);
	}
	int dwWeight;
	AnalysisResult oAnalysisResult;
};

class ControlDialog : public QDialog {
    // This Qt macro "Q_OBJECT" tells the Qt Add-in to invoke the moc compiler
    Q_OBJECT
public:
    ControlDialog();
	void SetupFunctionSelector();
	void SetupProgressView();

private:
	SlidingStackedWidget *lpSlider;
	QWidget *lpFunctionSelector;
	QWidget *lpProgressView;
	QLabel* lpProgressHeader;
	QPushButton *lpBatchButton;
	QPushButton *lpNextButton;
	QPushButton *lpPrevButton;
	QPushButton *lpCancelButton;
	QProgressBar *lpProgressBar;
    FunctionList* lpFunctionList;
	QTableWidget *lpResultsTable;
    QGridLayout* lpLayout;
	CoordinatorThread *lpCoordinatorThread;
	std::list<SignatureDefinition> aSignatureList;
	int dwNumFunctions;
	int dwNumFunctionsLeft;

	void ConstructSignatures();
	void StartAnalysis(bool bBatchRun);
	void HandleResult(bool bBatchRun, AnalysisResult oResult);

private slots:
	void StartAnalysis();
	void StartBatchRun();
	void HandleResultNormal(AnalysisResult oResult);
	void HandleResultBatch(AnalysisResult oResult);
	void CoordinatorFinished();
	void NextFunction();
	void DisplayResult(const QModelIndex& index);

signals:
	void PerformAnalysis(const std::list<unsigned long> &aFunctions);
	void slideInIdx(int idx);
};