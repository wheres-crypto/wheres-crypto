#include <windows.h>

#include <ida.hpp>
#include <idp.hpp>
#include <graph.hpp>
#include <loader.hpp>
#include <kernwin.hpp>

#include "common.hpp"
#include "Arm.hpp"
#include "Broker.hpp"
#include "ThreadPool.hpp"
#include "PathOracle.hpp"
#include "DFGraph.hpp"
#include "DFGDisplay.hpp"
#include "Predicate.hpp"
#include "ThreadPool.hpp"

#include "ControlDialog.hpp"

class DFGPlugin {
public:
	DFGPlugin();
	~DFGPlugin();
	static int DfgMenuCallback(void);
};

static DFGPlugin g_oDFGPlugin;

class dfgplugin_ah_t: public action_handler_t {
public:
	int (*lpCallback)(void);

	dfgplugin_ah_t(int (*lpCallback)(void)): lpCallback(lpCallback) { }
	~dfgplugin_ah_t() { }
	int idaapi activate(action_activation_ctx_t *lpCtx) { return lpCallback(); }
	action_state_t idaapi update(action_update_ctx_t *lpCtx) { return AST_ENABLE_ALWAYS; }
};

DFGPlugin::DFGPlugin() { }
DFGPlugin::~DFGPlugin() { }

int DFGPlugin::DfgMenuCallback(void) {
	ControlDialog dlg;
	dlg.exec();

	return 0;
}

int idaapi init(void) {
	static dfgplugin_ah_t oMenuHandler(DFGPlugin::DfgMenuCallback);
	static action_desc_t oMenuAction = {
		/*.cb =*/ sizeof(struct action_desc_t),
		/*.name =*/ "WheresCrypto:Dialog",
		/*.label =*/ "Where's Crypto?",
		/*.handler =*/ &oMenuHandler,
		/*.owner =*/ NULL,
		/*.shortcut = */ NULL,
		/*.tooltip =*/ NULL,
		/*.icon = */ load_custom_icon(":/images/wherescrypto.png"),
		/*.flags =*/ 0
	};
	if (register_action(oMenuAction)) {
		attach_action_to_menu("File/", oMenuAction.name, SETMENU_APP);
	} else {
		wc_debug("[-] Unable to register action '%s'", oMenuAction.name);
	}
	return PLUGIN_KEEP;
}

void idaapi term(void) { }

bool idaapi run(size_t /*arg*/) { return true; }

//--------------------------------------------------------------------------
char comment[] = "where's crypto";

char help[] = "";

//--------------------------------------------------------------------------
// This is the preferred name of the plugin module in the menu system
// The preferred name may be overriden in plugins.cfg file

char wanted_name[] = "Where's Crypto?";


// This is the preferred hotkey for the plugin module
// The preferred hotkey may be overriden in plugins.cfg file
// Note: IDA won't tell you if the hotkey is not correct
//       It will just disable the hotkey.

char wanted_hotkey[] = "";

plugin_t PLUGIN = {
  IDP_INTERFACE_VERSION,
  0,                    // plugin flags
  init,                 // initialize

  term,                 // terminate. this pointer may be NULL.

  run,                  // invoke plugin

  comment,              // long comment about the plugin
						// it could appear in the status line
						// or as a hint

  help,                 // multiline help about the plugin

  wanted_name,          // the preferred short name of the plugin
  wanted_hotkey         // the preferred hotkey to run the plugin
};