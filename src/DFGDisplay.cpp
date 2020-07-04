#include "DFGDisplay.hpp"
#include "DFGraph.hpp"
#include "DFGNode.hpp"
#include "SignatureEvaluator.hpp"
#include "AnalysisResult.hpp"

void DFGDisplayImpl::Display() {
	netnode id;
	id.create((std::string("$ dfgraph ") + szTitle).c_str());
	lpViewer = create_graph_viewer(szTitle.c_str(), id, gr_callback, this, 0);
	display_widget(lpViewer, WOPN_TAB);
	ref();
}

ssize_t idaapi DFGDisplayImpl::gr_callback(void *lpPrivate, int dwCode, va_list va) {
	DFGDisplayImpl *self = (DFGDisplayImpl *)lpPrivate;
	bool bResult = false;

	switch (dwCode) {
	case grcode_user_refresh: {
		if (self->bNeedRefresh) {
			mutable_graph_t *lpMutable = va_arg(va, mutable_graph_t *);
			std::unordered_map<DFGNode, int> aReverseLookup;
			DFGraphImpl::iterator it;
			self->bNeedRefresh = false;

			lpMutable->reset();
			lpMutable->resize(self->oGraph->size());

			/* create nodes */
			int dwNodeId = 0;
			self->clear();
			self->reserve(self->oGraph->size());
			aReverseLookup.reserve(self->oGraph->size());
			/* link them into unordered_map and aReverseLookup */
			for (it = self->oGraph->begin(); it != self->oGraph->end(); it++) {
				self->insert(std::pair<int, DFGNode>(dwNodeId, it->second));
				aReverseLookup.insert(std::pair<DFGNode, int>(it->second, dwNodeId));
				dwNodeId++;
			}

			for (it = self->oGraph->begin(); it != self->oGraph->end(); it++) {
				std::unordered_map<unsigned int, DFGNode>::iterator itParent;
				int dwNode1 = aReverseLookup.find(it->second)->second;
				for (
					itParent = it->second->aInputNodesUnique.begin();
					itParent != it->second->aInputNodesUnique.end();
					itParent++
				) {

					int dwNode2 = aReverseLookup.find(itParent->second)->second;
					lpMutable->add_edge(dwNode2, dwNode1, NULL);
				}
			}
		}
		bResult = true;
		break;
	}
	case grcode_user_text: {
		mutable_graph_t *lpMutable va_arg(va, mutable_graph_t *);
		unsigned int dwNodeId = va_arg(va, unsigned int);
		const char **lpText = va_arg(va, const char **);
		bgcolor_t *lpBgcolor = va_arg(va, bgcolor_t *);

		DFGDisplayImpl::iterator it = self->find(dwNodeId);
		if (it != self->end()) {
			*lpText = qstrdup(it->second->mnemonic().c_str());
			if (lpBgcolor != NULL) {
				if (self->oEvaluationResult != nullptr && self->oEvaluationResult->Mark(it->second)) {
					*lpBgcolor = 0x0000ff;
				}
			}
			bResult = true;
		} else {
			bResult = false;
		}
		break;
	}
	case grcode_destroyed: {
		self->unref();
	}
	}
	return bResult;
}