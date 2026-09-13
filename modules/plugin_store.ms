-- plugin_store.ms - Simple plugin manager (enable/disable storing small state)
-- Generated: 2025-09-21

try(destroyDialog PluginStore)catch()
rollout PluginStore "Plugin Store" width:420 height:220
(
    listbox lb_plugins "" width:400 height:140 items:#("core","ai","exporter")
    button b_enable "Enable Selected"
    button b_disable "Disable Selected"
    on b_enable pressed do ( messageBox ("Enabled: " + lb_plugins.items[lb_plugins.selection]) )
    on b_disable pressed do ( messageBox ("Disabled: " + lb_plugins.items[lb_plugins.selection]) )
)
createDialog PluginStore
