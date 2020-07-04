#pragma once

#include <list>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>

class function_list_item_t;

class function_list_item_label_t: public QTableWidgetItem {
public:
	inline function_list_item_label_t(const QIcon &icon, const QString &label, function_list_item_t *lpItem)
		: QTableWidgetItem(icon, label), lpListItem(lpItem) { }
	inline function_list_item_label_t(const QString &label, function_list_item_t *lpItem)
		: QTableWidgetItem(label), lpListItem(lpItem) { }
	inline ~function_list_item_label_t() { }

	function_list_item_t *lpListItem;
};

class function_list_item_t {
public:
	QString szFunctionName;
	unsigned long lpAddress;

	function_list_item_label_t *lpNameItem;
	function_list_item_label_t *lpAddressItem;
	QTableWidget *lpParent;

	function_list_item_t(unsigned long lpAddress, QTableWidget *lpParent);
	~function_list_item_t();
};

class FunctionList : public QTableWidget, public std::list<function_list_item_t *> {
	// This Qt macro "Q_OBJECT" tells the Qt Add-in to invoke the moc compiler
	Q_OBJECT
public:

	FunctionList();
	~FunctionList();

	std::list<unsigned long> GetSelection();
};