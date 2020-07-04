#include <unordered_map>
#include <sstream>
#include <ida.hpp>
#include <funcs.hpp>
#include <idp.hpp>

#include <QtWidgets/QHeaderView>

#include "FunctionList.hpp"
#include "ThreadPool.hpp"

function_list_item_t::function_list_item_t(unsigned long lpAddress, QTableWidget *lpParent) {
	qstring szFunctionName;
	char szAddressHex[16];

	API_LOCK();
	get_func_name(&szFunctionName, lpAddress & ~1);
	API_UNLOCK();
	if (szFunctionName.length() == 0) {
		std::stringstream oStream;
		oStream << "sub_";
		oStream << std::hex << lpAddress;
		szFunctionName = oStream.str().c_str();
	}
	this->szFunctionName = szFunctionName.c_str();
	this->lpAddress = lpAddress;
	::qsnprintf(szAddressHex, 16, "%08x", lpAddress);

	this->lpNameItem = new function_list_item_label_t(QIcon(":/images/function.png"), this->szFunctionName, this);
	this->lpAddressItem = new function_list_item_label_t(szAddressHex, this);
	this->lpParent = lpParent;

	int dwNumRows = lpParent->rowCount();
	lpParent->setRowCount(dwNumRows + 1);
	lpParent->setItem(dwNumRows, 0, lpNameItem);
	lpParent->setItem(dwNumRows, 1, lpAddressItem);
	lpNameItem->setFlags(lpNameItem->flags() & ~Qt::ItemIsEditable);
	lpAddressItem->setFlags(lpAddressItem->flags() & ~Qt::ItemIsEditable);
}

function_list_item_t::~function_list_item_t() {
	//int dwRowNo = lpParent->row(lpNameItem);
	//if (dwRowNo != -1) {
	//	lpParent->removeRow(dwRowNo);
	//}
	//delete lpNameItem;
	//delete lpAddressItem;
}

FunctionList::FunctionList() {
	QStringList aColumns;
	size_t dwNumFunctions;
	size_t i;

	setColumnCount(2);
	setShowGrid(false);
	verticalHeader()->hide();
	verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	verticalHeader()->setDefaultSectionSize(20);
	aColumns.push_back("Function name");
	aColumns.push_back("Start");
	setHorizontalHeaderLabels(aColumns);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSortingEnabled(false);
	setStyleSheet(" \
		QTableWidget: { border-collapse: collapse; }; \
		QTableWidgetItem: { padding: 0px; margin: 0px; }; \
	");

	API_LOCK();
	dwNumFunctions = get_func_qty();
	API_UNLOCK();

	for (i = 0; i < dwNumFunctions; i++) {
		segment_t *lpSegment(NULL);
		qstring szSegmentName;
		API_LOCK();
		func_t *lpCurrentFunction = getn_func(i);
		if (lpSegment == NULL ||
			lpSegment->start_ea > lpCurrentFunction->start_ea ||
			lpSegment->end_ea <= lpCurrentFunction->start_ea) {

			lpSegment = getseg(lpCurrentFunction->start_ea);
			if (lpSegment != NULL) {
				get_segm_name(&szSegmentName, lpSegment);
			}else {
				szSegmentName = "";
			}
		}
		API_UNLOCK();

		if (lpSegment == NULL || szSegmentName != "extern") {
			push_back(new function_list_item_t(lpCurrentFunction->start_ea, this));
		}
	}

	resizeColumnToContents(0);
	setSortingEnabled(true);
	//QHeaderView *lpHeader = horizontalHeader();
	//connect(lpHeader, &QHeaderView::sectionClicked, [this](int dwIndex) {
	//	sortByColumn(dwIndex);
	//});
}

FunctionList::~FunctionList() {
	FunctionList::iterator it;
	for (it = begin(); it != end(); it++) {
		delete *it;
	}
}

std::list<unsigned long> FunctionList::GetSelection() {
	std::list<unsigned long> aList;
	QList<QTableWidgetItem *> aSelected;
	QList<QTableWidgetItem *>::iterator it;
	std::unordered_map<int, char> aItemMap;

	aSelected = selectedItems();
	for (it = aSelected.begin(); it != aSelected.end(); it++) {
		int dwRowNo = row(*it);
		if (aItemMap.find(dwRowNo) == aItemMap.end()) {
			aItemMap.insert(std::pair<int, char>(dwRowNo, 0));
			
			aList.push_back(((function_list_item_label_t *)(*it))->lpListItem->lpAddress);
		}
	}

	return aList;
}