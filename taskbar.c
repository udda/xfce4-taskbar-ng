
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <exo/exo.h>
#include <libxfce4ui/libxfce4ui.h>
#include <panel-xfconf.h>
#include <panel-utils.h>
#include <panel-private.h>
#include <libxfce4panel/libxfce4panel.h>

#include "taskbar-widget.h"
#include "taskbar-dialog_ui.h"

/* TODO move to header */
GType taskbar_plugin_get_type (void) G_GNUC_CONST;
void taskbar_plugin_register_type (XfcePanelTypeModule *type_module);
#define XFCE_TYPE_TASKBAR_PLUGIN            (taskbar_plugin_get_type ())
#define XFCE_TASKBAR_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XFCE_TYPE_TASKBAR_PLUGIN, TaskBarPlugin))
#define XFCE_TASKBAR_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), XFCE_TYPE_TASKBAR_PLUGIN, TaskBarPluginClass))
#define XFCE_IS_TASKBAR_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XFCE_TYPE_TASKBAR_PLUGIN))
#define XFCE_IS_TASKBAR_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), XFCE_TYPE_TASKBAR_PLUGIN))
#define XFCE_TASKBAR_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), XFCE_TYPE_TASKBAR_PLUGIN, TaskBarPluginClass))


typedef struct _TaskBarPluginClass TaskBarPluginClass;
struct _TaskBarPluginClass
{
  XfcePanelPluginClass __parent__;
};

typedef struct _TaskBarPlugin TaskBarPlugin;
struct _TaskBarPlugin
{
  XfcePanelPlugin __parent__;

  /* the taskbar widget */
  GtkWidget     *taskbar;
  GtkWidget     *handle;
};

struct XfceTaskBar ;

static void     taskbar_plugin_construct               (XfcePanelPlugin *panel_plugin);
static void     taskbar_plugin_orientation_changed     (XfcePanelPlugin *panel_plugin, GtkOrientation orientation);
static gboolean taskbar_plugin_size_changed            (XfcePanelPlugin *panel_plugin, gint size);
static void     taskbar_plugin_screen_position_changed (XfcePanelPlugin *panel_plugin, XfceScreenPosition position);
static void     taskbar_plugin_configure_plugin        (XfcePanelPlugin    *panel_plugin);
static gboolean taskbar_plugin_handle_expose_event     (GtkWidget *widget, GdkEventExpose *event, TaskBarPlugin *plugin);

/* define and register the plugin */
XFCE_PANEL_DEFINE_PLUGIN_RESIDENT (TaskBarPlugin, taskbar_plugin)

static void taskbar_plugin_class_init (TaskBarPluginClass *klass)
{
  XfcePanelPluginClass *plugin_class;

  plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
  plugin_class->construct = taskbar_plugin_construct;
  plugin_class->orientation_changed = taskbar_plugin_orientation_changed;
  plugin_class->size_changed = taskbar_plugin_size_changed;
  plugin_class->screen_position_changed = taskbar_plugin_screen_position_changed;
  plugin_class->configure_plugin = taskbar_plugin_configure_plugin;
}

static void taskbar_plugin_init (TaskBarPlugin *plugin)
{
  GtkWidget *box;

  /* create widgets */
  box = xfce_hvbox_new (GTK_ORIENTATION_HORIZONTAL, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (plugin), box);
  exo_binding_new (G_OBJECT (plugin), "orientation", G_OBJECT (box), "orientation");
  gtk_widget_show (box);

  plugin->handle = gtk_alignment_new (0.00, 0.00, 0.00, 0.00);
  gtk_box_pack_start (GTK_BOX (box), plugin->handle, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (plugin->handle), "expose-event",
      G_CALLBACK (taskbar_plugin_handle_expose_event), plugin);
  gtk_widget_set_size_request (plugin->handle, 8, 8);
  gtk_widget_show (plugin->handle);
  
  /* Through some glib contortions, the plugin is initialised here */
  plugin->taskbar = g_object_new (XFCE_TYPE_taskbar, NULL);
  
  gtk_box_pack_start (GTK_BOX (box), plugin->taskbar, TRUE, TRUE, 0);
  
  exo_binding_new (G_OBJECT (plugin->taskbar), "show-handle", G_OBJECT (plugin->handle), "visible");
}

static void taskbar_plugin_construct (XfcePanelPlugin *panel_plugin)
{
  char *rc_path ;
  TaskBarPlugin      *plugin = XFCE_TASKBAR_PLUGIN (panel_plugin);
  const PanelProperty  properties[] =
  {
    { "include-all-workspaces", G_TYPE_BOOLEAN },
    { "include-all-monitors", G_TYPE_BOOLEAN },
    { "switch-workspace-on-unminimize", G_TYPE_BOOLEAN },
    { "show-only-minimized", G_TYPE_BOOLEAN },
    { "show-wireframes", G_TYPE_BOOLEAN },
    { "show-handle", G_TYPE_BOOLEAN },
    { "show-instances-on-hover", G_TYPE_BOOLEAN },
    { "drag-button", G_TYPE_INT },
    { "flat-buttons", G_TYPE_BOOLEAN },
    { NULL }
  };

  /* show configure */
  xfce_panel_plugin_menu_show_configure (XFCE_PANEL_PLUGIN (plugin));

  /* expand the plugin */
  xfce_panel_plugin_set_expand (panel_plugin, TRUE);

  /* bind all properties */
  panel_properties_bind (NULL, G_OBJECT (plugin->taskbar), xfce_panel_plugin_get_property_base (panel_plugin), properties, FALSE);
  
  /* show the taskbar */
  gtk_widget_show (plugin->taskbar);
}

static void taskbar_plugin_orientation_changed (XfcePanelPlugin *panel_plugin, GtkOrientation orientation)
{
  TaskBarPlugin *plugin = XFCE_TASKBAR_PLUGIN (panel_plugin);

  /* set the new taskbar orientation */
  xfce_taskbar_set_orientation (XFCE_taskbar (plugin->taskbar), orientation);
}

static gboolean taskbar_plugin_size_changed (XfcePanelPlugin *panel_plugin, gint size)
{
  TaskBarPlugin *plugin = XFCE_TASKBAR_PLUGIN (panel_plugin);

  /* set the taskbar size */
  xfce_taskbar_set_size (XFCE_taskbar (plugin->taskbar), size);

  return TRUE;
}

static void taskbar_plugin_screen_position_changed (XfcePanelPlugin *panel_plugin, XfceScreenPosition  position)
{
  TaskBarPlugin *plugin = XFCE_TASKBAR_PLUGIN (panel_plugin);

  /* update monitor geometry; this function is also triggered when
   * the panel is moved to another monitor during runtime */
  xfce_taskbar_update_monitor_geometry (XFCE_taskbar (plugin->taskbar));
}

static void taskbar_plugin_configure_plugin (XfcePanelPlugin *panel_plugin)
{
  TaskBarPlugin *plugin = XFCE_TASKBAR_PLUGIN (panel_plugin);
  GtkBuilder     *builder;
  GObject        *dialog;
  GObject        *object;
  GtkTreeIter     iter;

  /* setup the dialog */
  PANEL_UTILS_LINK_4UI
  builder = panel_utils_builder_new (panel_plugin, taskbar_dialog_ui, -1, &dialog);
  if (G_UNLIKELY (builder == NULL))
    return;

#define TASKBAR_DIALOG_BIND(name, property) \
  object = gtk_builder_get_object (builder, (name)); \
  panel_return_if_fail (G_IS_OBJECT (object)); \
  exo_mutual_binding_new (G_OBJECT (plugin->taskbar), (name), \
                          G_OBJECT (object), (property));

#define TASKBAR_DIALOG_BIND_INV(name, property) \
  object = gtk_builder_get_object (builder, (name)); \
  panel_return_if_fail (G_IS_OBJECT (object)); \
  exo_mutual_binding_new_with_negation (G_OBJECT (plugin->taskbar), \
                                        name,  G_OBJECT (object), \
                                        property);

  TASKBAR_DIALOG_BIND ("include-all-workspaces", "active")
  TASKBAR_DIALOG_BIND ("include-all-monitors", "active")
  TASKBAR_DIALOG_BIND_INV ("switch-workspace-on-unminimize", "active")
  TASKBAR_DIALOG_BIND ("show-only-minimized", "active")
  TASKBAR_DIALOG_BIND ("show-wireframes", "active")
  TASKBAR_DIALOG_BIND ("show-handle", "active")
  TASKBAR_DIALOG_BIND ("show-instances-on-hover", "active")
  TASKBAR_DIALOG_BIND ("drag-button", "active")
  TASKBAR_DIALOG_BIND ("flat-buttons", "active")

#ifndef GDK_WINDOWING_X11
  /* not functional in x11, so avoid confusion */
  object = gtk_builder_get_object (builder, "show-wireframes");
  gtk_widget_hide (GTK_WIDGET (object));
#endif

  gtk_widget_show (GTK_WIDGET (dialog));
}

static gboolean taskbar_plugin_handle_expose_event (GtkWidget *widget, GdkEventExpose *event, TaskBarPlugin *plugin)
{
  GtkOrientation orientation;

  panel_return_val_if_fail (XFCE_IS_TASKBAR_PLUGIN (plugin), FALSE);
  panel_return_val_if_fail (plugin->handle == widget, FALSE);

  if (!GTK_WIDGET_DRAWABLE (widget))
    return FALSE;

  /* get the orientation */
  if (xfce_panel_plugin_get_orientation (XFCE_PANEL_PLUGIN (plugin)) ==
      GTK_ORIENTATION_HORIZONTAL)
    orientation = GTK_ORIENTATION_VERTICAL;
  else
    orientation = GTK_ORIENTATION_HORIZONTAL;

  /* paint the handle */
  gtk_paint_handle (widget->style, widget->window,
                    GTK_WIDGET_STATE (widget), GTK_SHADOW_NONE,
                    &(event->area), widget, "handlebox",
                    widget->allocation.x,
                    widget->allocation.y,
                    widget->allocation.width,
                    widget->allocation.height,
                    orientation);

  return TRUE;
}
