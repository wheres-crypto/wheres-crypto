#pragma once

#include <unordered_map>
#include <string>

#include <ida.hpp>
#include <idp.hpp>
#include <graph.hpp>

#include "types.hpp"

class DFGDisplayImpl
	: virtual public ReferenceCounted,
	  public std::unordered_map<int, DFGNode> {
public:
	DFGDisplayImpl(const std::string &szTitle, DFGraph &oGraph, AbstractEvaluationResult oEvaluationResult = nullptr)
		: szTitle(szTitle), oGraph(oGraph), oEvaluationResult(oEvaluationResult), lpViewer(NULL), bNeedRefresh(true) { }
	void Display();

	static ssize_t idaapi gr_callback(void *ud, int code, va_list va);

	DFGraph oGraph;
	AbstractEvaluationResult oEvaluationResult;
	std::string szTitle;
	graph_viewer_t *lpViewer;
	bool bNeedRefresh;
};