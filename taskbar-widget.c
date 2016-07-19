#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_MATH_H
#include <math.h>
#endif

#define WNCK_I_KNOW_THIS_IS_UNSTABLE

#include <gtk/gtk.h>
#include <exo/exo.h>
#include <libwnck/libwnck.h>
#include <libxfce4panel/libxfce4panel.h>
#include <panel-private.h>
#include <panel-debug.h>

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <X11/extensions/shape.h>
#endif

#include "taskbar-widget.h"
#include "hotkeys.h"

#define DEFAULT_BUTTON_SIZE             (25)
#define DEFAULT_MAX_BUTTON_LENGTH       (400)
#define DEFAULT_MENU_ICON_SIZE          (16)
#define DEFAULT_MIN_BUTTON_LENGTH       (DEFAULT_MAX_BUTTON_LENGTH / 4)
#define DEFAULT_ICON_LUCENCY            (50)
#define DEFAULT_ELLIPSIZE_MODE          (PANGO_ELLIPSIZE_END)
#define DEFAULT_MENU_MAX_WIDTH_CHARS    (50)
#define ARROW_BUTTON_SIZE               (20)
#define WIREFRAME_SIZE                  (5) /* same as xfwm4 */
#define DRAG_ACTIVATE_TIMEOUT           (500)
#define command_SIZE                 (512)
#define TOKEN_BUFFER_SIZE               (32)

/* locking helpers for taskbar->locked */
#define xfce_taskbar_lock(taskbar)            G_STMT_START { XFCE_taskbar (taskbar)->locked++; } G_STMT_END
#define xfce_taskbar_unlock(taskbar)        G_STMT_START { \
                           if (XFCE_taskbar (taskbar)->locked > 0) \
                               XFCE_taskbar (taskbar)->locked--; \
                           else \
                               panel_assert_not_reached (); \
                       } G_STMT_END
#define xfce_taskbar_is_locked(taskbar) (XFCE_taskbar (taskbar)->locked > 0)

#define xfce_taskbar_get_panel_plugin(taskbar) gtk_widget_get_ancestor (GTK_WIDGET (taskbar), XFCE_TYPE_PANEL_PLUGIN)
#define xfce_taskbar_horizontal(taskbar) ((taskbar)->horizontal)
#define xfce_taskbar_filter_monitors(taskbar) (!(taskbar)->all_monitors && (taskbar)->monitor_geometry.width != -1)
#define xfce_taskbar_geometry_set_invalid(taskbar) ((taskbar)->monitor_geometry.width = -1)
#define xfce_taskbar_geometry_has_point(taskbar, x, y) ( \
    (x) >= ((taskbar)->monitor_geometry.x) \
    && (x) < ((taskbar)->monitor_geometry.x + (taskbar)->monitor_geometry.width) \
    && (y) >= ((taskbar)->monitor_geometry.y) \
    && (y) < ((taskbar)->monitor_geometry.y + (taskbar)->monitor_geometry.height))

enum
{
    PROP_0,
    PROP_INCLUDE_ALL_WORKSPACES,
    PROP_INCLUDE_ALL_MONITORS,
    PROP_SWITCH_WORKSPACE_ON_UNMINIMIZE,
    PROP_SHOW_ONLY_MINIMIZED,
    PROP_SHOW_WIREFRAMES,
    PROP_SHOW_HANDLE,
    PROP_SHOW_INSTANCES_HOVER,
    PROP_DRAG_BUTTON,
    PROP_FLAT_BUTTONS,
};


enum {LEFTMOUSE=1, MIDMOUSE=2, RIGHTMOUSE=3} ;
enum { HOVER_DISPLAY_COOLOFF=200, GROUP_ICON_HOVER_TIMEOUT=250 } ;

struct _XfceTaskBarClass
{
    GtkContainerClass __parent__;
};

struct _XfceTaskBar
{
    GtkContainer __parent__;

    /* lock counter */
    gint locked;

    /* the screen of this taskbar */
    WnckScreen  *screen;
    GdkScreen   *gdk_screen;

    /* window children in the taskbar */
    GList *wgroups;

    /* windows we monitor, but that are excluded from the taskbar */
    GSList *skipped_windows;

    /* classgroups of all the windows in the taskbar */
    GHashTable *groups;

    /* flag to indicate in-progress drag */
    gboolean dragactive ;
    
    /* timestamp for filtering glitchy enter events generated during drag and drop */
    guint dragtimestamp ;

    /* size of the panel plugin */
    gint size;

    /* orientation of the taskbar */
    guint horizontal : 1;

    /* whether we show windows from all workspaces or
     * only the active workspace */
    guint all_workspaces : 1;

    /* whether we switch to another workspace when we try to
     * unminimize a window on another workspace */
    guint switch_workspace : 1;

    /* whether we only show monimized windows in the
     * taskbar */
    guint only_minimized : 1;

    /* whether we only show windows that are in the geometry of
     * the monitor the taskbar is on */
    guint           all_monitors : 1;
    GdkRectangle    monitor_geometry;

    /* whether we show wireframes when hovering a button in
     * the taskbar */
    guint show_wireframes : 1;

    /* icon geometries update timeout */
    guint update_icon_geometries_id;

    /* idle monitor geometry update */
    guint update_monitor_geometry_id;

    /* dummy properties */
    guint show_handle : 1;
    guint show_instances_hover : 1;
    guint drag_button; /* Middle */
    guint flat_buttons : 1;

    guint unique_id_counter ;
    
    gchar *rc_path ;
    HotkeysHandler *hotkeys_handler;

#ifdef GDK_WINDOWING_X11
    // wireframe window
    Window wireframe_window;
#endif

    // gtk style properties
    gint                    max_button_length;
    gint                    min_button_length;
    gint                    max_button_size;
    PangoEllipsizeMode      ellipsize_mode;
    gint                    minimized_icon_lucency;
    gint                    menu_icon_size;
    gint                    menu_max_width_chars;
    
    gint n_group_icons;
};

typedef struct _XfceTaskBarGroup XfceTaskBarGroup;
typedef struct _XfceTaskBarWNode XfceTaskBarWNode;

struct _XfceTaskBarWNode
{
    XfceTaskBar         *taskbar;

    GtkWidget           *icon;
    GtkWidget           *label;

    WnckWindow          *window;
    gchar               *group_name;
    XfceTaskBarGroup    *group ;
    
    gboolean            visible ;
    /* last activated */
    guint64		timestamp;
};

struct _XfceTaskBarGroup
{
    XfceTaskBar   *taskbar;
    GtkWidget     *button;
    GtkWidget     *align;
    GtkWidget     *icon;
    GdkPixbuf     *pixbuf ;
    GSList        *wnodes;
    gchar         *window_class_name ;
    guint         unique_id ;
    gboolean      pinned ;
    gchar         *command ;
    guint         hover_timeout ;
    guint         hover_visible_timestamp ;
};

#define DISABLE_HOVER_TIMEOUT(group) if(group->hover_timeout != 0) {g_source_remove(group->hover_timeout); group->hover_timeout=0;}

static const GtkTargetEntry source_targets[] =
{
    { "application/x-wnck-window-id", 0, 0 }
};

static void             xfce_taskbar_get_property               (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void             xfce_taskbar_set_property               (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void             xfce_taskbar_finalize                   (GObject *object);
static void             xfce_taskbar_size_request               (GtkWidget *widget, GtkRequisition *requisition);
static void             xfce_taskbar_size_allocate              (GtkWidget *widget, GtkAllocation *allocation);
static void             xfce_taskbar_style_set                  (GtkWidget *widget, GtkStyle *previous_style);
static void             xfce_taskbar_realize                    (GtkWidget *widget);
static void             xfce_taskbar_unrealize                  (GtkWidget *widget);
static void             xfce_taskbar_remove                     (GtkContainer *container, GtkWidget *widget);
static void             xfce_taskbar_forall                     (GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data);
static GType            xfce_taskbar_child_type                 (GtkContainer *container);
static void             xfce_taskbar_connect_screen             (XfceTaskBar*taskbar);
static void             xfce_taskbar_disconnect_screen          (XfceTaskBar *taskbar);
static void             xfce_taskbar_gdk_screen_changed         (GdkScreen *gdk_screen, XfceTaskBar *taskbar);
static void             xfce_taskbar_active_window_changed      (WnckScreen *screen, WnckWindow *previous_window, XfceTaskBar *taskbar);
static void             xfce_taskbar_active_workspace_changed   (WnckScreen *screen, WnckWorkspace *previous_workspace, XfceTaskBar *taskbar);
static void             xfce_taskbar_window_added               (WnckScreen *screen, WnckWindow *window, XfceTaskBar *taskbar);
static void             xfce_taskbar_window_removed             (WnckScreen *screen, WnckWindow *window, XfceTaskBar *taskbar);
static void             xfce_taskbar_viewports_changed          (WnckScreen *screen, XfceTaskBar *taskbar);
static gboolean         xfce_taskbar_update_icon_geometries     (gpointer data);

static void             xfce_taskbar_skipped_windows_state_changed (WnckWindow *window, WnckWindowState changed_state, WnckWindowState new_state, XfceTaskBar *taskbar);
static void             xfce_taskbar_update_icon_geometries_destroyed (gpointer data);

// wireframe
#ifdef GDK_WINDOWING_X11
static void xfce_taskbar_wireframe_hide     (XfceTaskBar *taskbar);
static void xfce_taskbar_wireframe_destroy  (XfceTaskBar *taskbar);
static void xfce_taskbar_wireframe_update   (XfceTaskBar *taskbar, XfceTaskBarWNode *child);
#endif

// taskbar buttons
static inline gboolean      xfce_taskbar_button_visible         (XfceTaskBarWNode *child, WnckWorkspace *active_ws);
static GtkWidget*           xfce_taskbar_button_proxy_menu_item (XfceTaskBarWNode *child, gboolean allow_wireframe);
static void                 xfce_taskbar_button_activate        (XfceTaskBarWNode *child, guint32 timestamp);
static XfceTaskBarWNode*    xfce_taskbar_wnode_new              (WnckWindow *window, XfceTaskBar *taskbar);
static void                 xfce_taskbar_wnode_del              (XfceTaskBarWNode *wnode);

// taskbar group buttons
static int          xfce_taskbar_group_visible_count            (XfceTaskBarGroup *group, WnckWorkspace *active_ws);
static void         xfce_taskbar_group_update_visibility        (XfceTaskBarGroup *group);
static void         xfce_taskbar_group_button_remove            (XfceTaskBarGroup *group);
static void         xfce_taskbar_group_button_add_window        (XfceTaskBarGroup *group, XfceTaskBarWNode *window_child);
static gboolean     xfce_taskbar_group_button_enter_event       (GtkWidget *button, GdkEvent *event, XfceTaskBarGroup *group);
static gboolean     xfce_taskbar_group_button_leave_event       (GtkWidget *button, GdkEvent *event, XfceTaskBarGroup *group);
static void        xfce_taskbar_group_button_menu_destroy      (GtkWidget *menu_widget, XfceTaskBarGroup *group);

static void xfce_taskbar_disable_group_enter_event(XfceTaskBar *taskbar);
static void xfce_taskbar_enable_group_enter_event(XfceTaskBar *taskbar);

static XfceTaskBarGroup* xfce_taskbar_group_button_new (const char *, XfceTaskBar *taskbar);

// pinning functions
static void     xfce_taskbar_group_button_toggle_pinned     (XfceTaskBarGroup *group);
static void     xfce_taskbar_group_button_launch_pinned     (XfceTaskBarGroup *group);
static void     xfce_taskbar_group_button_build_launch_menu (XfceTaskBarGroup *group, GtkWidget *menu, gboolean use_sep);
static void     xfce_taskbar_group_button_build_pin_menu    (XfceTaskBarGroup *group, GtkWidget *menu);
static void     cache_pinned_configuration                  (XfceTaskBar *taskbar);

//hover menu functions
static gboolean trigger_hover_menu_timeout(GtkWidget *widget, GdkEvent  *event, gpointer menu_ptr);
static gboolean xfce_taskbar_hover_menu_leave(GtkWidget *widget, GdkEvent  *event, gpointer menu_ptr);
static gboolean xfce_taskbar_hover_menu_enter(GtkWidget *widget, GdkEvent  *event, gpointer menu_ptr);
static gboolean xfce_taskbar_hover_menu_timeout(gpointer menu_ptr);
static gboolean xfce_taskbar_group_button_hover_timeout(gpointer group_ptr);
static void    xfce_taskbar_activate_hover_menu(GtkWidget *widget, XfceTaskBarGroup *group, size_t mouse_button);
static void    xfce_taskbar_disable_hover_menu_timeout(GtkWidget *menu_widget);

// potential public functions
static void xfce_taskbar_set_include_all_workspaces     (XfceTaskBar *taskbar, gboolean all_workspaces);
static void xfce_taskbar_set_include_all_monitors       (XfceTaskBar *taskbar, gboolean all_monitors);
static void xfce_taskbar_set_show_only_minimized        (XfceTaskBar *taskbar, gboolean only_minimized);
static void xfce_taskbar_set_show_wireframes            (XfceTaskBar *taskbar, gboolean show_wireframes);

G_DEFINE_TYPE (XfceTaskBar, xfce_taskbar, GTK_TYPE_CONTAINER)

static GtkIconSize menu_icon_size = GTK_ICON_SIZE_INVALID;

static void xfce_taskbar_class_init (XfceTaskBarClass *klass)
{
    GObjectClass            *gobject_class;
    GtkWidgetClass        *gtkwidget_class;
    GtkContainerClass *gtkcontainer_class;

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->get_property = xfce_taskbar_get_property;
    gobject_class->set_property = xfce_taskbar_set_property;
    gobject_class->finalize = xfce_taskbar_finalize;

    gtkwidget_class = GTK_WIDGET_CLASS (klass);
    gtkwidget_class->size_request = xfce_taskbar_size_request;
    gtkwidget_class->size_allocate = xfce_taskbar_size_allocate;
    gtkwidget_class->style_set = xfce_taskbar_style_set;
    gtkwidget_class->realize = xfce_taskbar_realize;
    gtkwidget_class->unrealize = xfce_taskbar_unrealize;

    gtkcontainer_class = GTK_CONTAINER_CLASS (klass);
    gtkcontainer_class->add = NULL;
    gtkcontainer_class->remove = xfce_taskbar_remove;
    gtkcontainer_class->forall = xfce_taskbar_forall;
    gtkcontainer_class->child_type = xfce_taskbar_child_type;

    g_object_class_install_property (gobject_class, PROP_INCLUDE_ALL_WORKSPACES, g_param_spec_boolean ("include-all-workspaces", NULL, NULL, FALSE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_INCLUDE_ALL_MONITORS, g_param_spec_boolean ("include-all-monitors", NULL, NULL, TRUE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_SWITCH_WORKSPACE_ON_UNMINIMIZE, g_param_spec_boolean ("switch-workspace-on-unminimize", NULL, NULL, TRUE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_SHOW_ONLY_MINIMIZED, g_param_spec_boolean ("show-only-minimized", NULL, NULL, FALSE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_SHOW_WIREFRAMES, g_param_spec_boolean ("show-wireframes", NULL, NULL, FALSE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_SHOW_HANDLE, g_param_spec_boolean ("show-handle", NULL, NULL, TRUE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_SHOW_INSTANCES_HOVER, g_param_spec_boolean ("show-instances-on-hover", NULL, NULL, TRUE, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_DRAG_BUTTON, g_param_spec_int ("drag-button", NULL, NULL, -1, G_MAXINT, 0, EXO_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, PROP_FLAT_BUTTONS, g_param_spec_boolean ("flat-buttons", NULL, NULL, TRUE, EXO_PARAM_READWRITE));
    
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_int ("max-button-length", NULL, "The maximum length of a window button", -1, G_MAXINT, DEFAULT_MAX_BUTTON_LENGTH, EXO_PARAM_READABLE));
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_int ("min-button-length", NULL, "The minumum length of a window button", 1, G_MAXINT, DEFAULT_MIN_BUTTON_LENGTH, EXO_PARAM_READABLE));
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_int ("max-button-size", NULL, "The maximum size of a window button", 1, G_MAXINT, DEFAULT_BUTTON_SIZE, EXO_PARAM_READABLE));
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_enum ("ellipsize-mode", NULL, "The ellipsize mode used for the button label", PANGO_TYPE_ELLIPSIZE_MODE, DEFAULT_ELLIPSIZE_MODE,EXO_PARAM_READABLE));
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_int ("minimized-icon-lucency", NULL, "Lucent percentage of minimized icons", 0, 100, DEFAULT_ICON_LUCENCY, EXO_PARAM_READABLE));
    gtk_widget_class_install_style_property (gtkwidget_class, g_param_spec_int ("menu-max-width-chars", NULL, "Maximum chars in the overflow menu labels", 0, G_MAXINT, DEFAULT_MENU_MAX_WIDTH_CHARS, EXO_PARAM_READABLE));

    menu_icon_size = gtk_icon_size_from_name ("panel-taskbar-menu");
    if (menu_icon_size == GTK_ICON_SIZE_INVALID)
    {
        menu_icon_size = gtk_icon_size_register ("panel-taskbar-menu", DEFAULT_MENU_ICON_SIZE, DEFAULT_MENU_ICON_SIZE);
    }
}



static void xfce_taskbar_init (XfceTaskBar *taskbar)
{
    GTK_WIDGET_SET_FLAGS (taskbar, GTK_NO_WINDOW);

    taskbar->locked = 0;
    taskbar->screen = NULL;
    taskbar->wgroups = NULL;
    taskbar->skipped_windows = NULL;
    taskbar->hotkeys_handler = NULL;
    taskbar->horizontal = TRUE;
    taskbar->all_workspaces = TRUE;
    taskbar->switch_workspace = TRUE;
    taskbar->only_minimized = FALSE;
    taskbar->show_wireframes = TRUE;
    taskbar->show_handle = TRUE;
    taskbar->show_instances_hover = FALSE;
    taskbar->drag_button = 1; /* Middle */
    taskbar->flat_buttons = 0;
    taskbar->all_monitors = TRUE;
    taskbar->unique_id_counter = 0x0 ;
    xfce_taskbar_geometry_set_invalid (taskbar);
#ifdef GDK_WINDOWING_X11
    taskbar->wireframe_window = 0;
#endif
    taskbar->update_icon_geometries_id = 0;
    taskbar->update_monitor_geometry_id = 0;
    taskbar->max_button_length = DEFAULT_MAX_BUTTON_LENGTH;
    taskbar->min_button_length = DEFAULT_MIN_BUTTON_LENGTH;
    taskbar->max_button_size = DEFAULT_BUTTON_SIZE;
    taskbar->minimized_icon_lucency = DEFAULT_ICON_LUCENCY;
    taskbar->ellipsize_mode = DEFAULT_ELLIPSIZE_MODE;
    taskbar->menu_icon_size = DEFAULT_MENU_ICON_SIZE;
    taskbar->menu_max_width_chars = DEFAULT_MENU_MAX_WIDTH_CHARS;
    
    //taskbar->groups = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
    taskbar->groups = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
    taskbar->dragactive = FALSE ;
    taskbar->dragtimestamp = 0 ;
}

static void xfce_taskbar_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    XfceTaskBar *taskbar = XFCE_taskbar (object);

    switch (prop_id)
    {
        case PROP_INCLUDE_ALL_WORKSPACES:
            g_value_set_boolean (value, taskbar->all_workspaces);
            break;

        case PROP_INCLUDE_ALL_MONITORS:
            g_value_set_boolean (value, taskbar->all_monitors);
            break;

        case PROP_SWITCH_WORKSPACE_ON_UNMINIMIZE:
            g_value_set_boolean (value, taskbar->switch_workspace);
            break;

        case PROP_SHOW_ONLY_MINIMIZED:
            g_value_set_boolean (value, taskbar->only_minimized);
            break;

        case PROP_SHOW_WIREFRAMES:
            g_value_set_boolean (value, taskbar->show_wireframes);
            break;

        case PROP_SHOW_HANDLE:
            g_value_set_boolean (value, taskbar->show_handle);
            break;

        case PROP_SHOW_INSTANCES_HOVER:
            g_value_set_boolean (value, taskbar->show_instances_hover);
            break;

        case PROP_DRAG_BUTTON:
            g_value_set_int (value, taskbar->drag_button);
            break;

        case PROP_FLAT_BUTTONS:
            g_value_set_boolean (value, taskbar->flat_buttons);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void xfce_taskbar_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    XfceTaskBar *taskbar = XFCE_taskbar (object);
    GList *gi ;
    
    switch (prop_id)
    {
        case PROP_INCLUDE_ALL_WORKSPACES:
            xfce_taskbar_set_include_all_workspaces (taskbar, g_value_get_boolean (value));
            break;

        case PROP_INCLUDE_ALL_MONITORS:
            xfce_taskbar_set_include_all_monitors (taskbar, g_value_get_boolean (value));
            break;

        case PROP_SWITCH_WORKSPACE_ON_UNMINIMIZE:
            taskbar->switch_workspace = g_value_get_boolean (value);
            break;

        case PROP_SHOW_ONLY_MINIMIZED:
            xfce_taskbar_set_show_only_minimized (taskbar, g_value_get_boolean (value));
            break;

        case PROP_SHOW_WIREFRAMES:
            xfce_taskbar_set_show_wireframes (taskbar, g_value_get_boolean (value));
            break;

        case PROP_SHOW_HANDLE:
            taskbar->show_handle = g_value_get_boolean (value);
            break;

        case PROP_SHOW_INSTANCES_HOVER:
            taskbar->show_instances_hover = g_value_get_boolean (value);
            break;

        case PROP_DRAG_BUTTON:
            taskbar->drag_button = g_value_get_int (value);
	    g_debug("drag_button=%d", taskbar->drag_button);
	    for(gi=taskbar->wgroups; gi!=NULL; gi=gi->next)
	    {
		    XfceTaskBarGroup *group =gi->data ;
		    gtk_drag_source_set (group->button, taskbar->drag_button ? GDK_BUTTON2_MASK : GDK_BUTTON1_MASK,            source_targets, G_N_ELEMENTS (source_targets), GDK_ACTION_MOVE);
	    }
            break;

        case PROP_FLAT_BUTTONS:
            taskbar->flat_buttons = g_value_get_boolean (value);
	    for(gi=taskbar->wgroups; gi!=NULL; gi=gi->next)
	    {
		    XfceTaskBarGroup *group =gi->data ;
		    xfce_taskbar_group_update_visibility(group);
	    }
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

void xfce_taskbar_load_pinned_config (XfceTaskBar *taskbar)
{
    char rc_path [256] ;
    int group_index=0x0 ;
    gchar **groups ;
    XfceRc *rc; 
    
    sprintf(rc_path, "%s/.config/xfce4/panel/taskbar/taskbar.rc", getenv("HOME"));
    
    rc = xfce_rc_simple_open (rc_path, TRUE);
    if(!rc)
    {
        return ;
    }
    
    groups = xfce_rc_get_groups(rc);
    
    //the first group is a dud, wtf?
    group_index = 1;
    while(groups[group_index])
    {
        GError *err = NULL;
        cairo_surface_t *surface ;
        char png_path [512] ;
        char *group_name = groups[group_index] ;
        const char *command_string ;
        
        
        xfce_rc_set_group (rc, group_name);
        
        XfceTaskBarGroup *group = xfce_taskbar_group_button_new (group_name, taskbar);
        
        sprintf(png_path, "%s/.config/xfce4/panel/taskbar/%s.png", getenv("HOME"), group_name);
        group->pixbuf = gdk_pixbuf_new_from_file(png_path, &err);
        if(!group->pixbuf)
        {
            //FIXME: free the residual memory in the group structure
            xfce_dialog_show_error (NULL, err, _("Failed to load the icon \"%s\""), png_path);
            return ;
        }
        g_object_ref(group->pixbuf);
        
        command_string = xfce_rc_read_entry (rc, "command", NULL) ;
        if(!command_string)
        {
            xfce_dialog_show_error (NULL, NULL, "TaskBar plugin failed to load the pinned command string!");
            return ;
        }
        group->command = g_strdup(command_string);
        group->pinned = TRUE ;
        
        //add in the icon and add it to the list of groups
        xfce_panel_image_set_from_pixbuf (XFCE_PANEL_IMAGE (group->icon), group->pixbuf);
        g_hash_table_insert (taskbar->groups, (gpointer)group->window_class_name, group);
        
        xfce_taskbar_group_update_visibility(group);
        
        group_index += 1 ;
    }
    
    xfce_rc_close (rc);
    
    gtk_widget_queue_resize (GTK_WIDGET (taskbar));
}

void xfce_taskbar_save_pinned_config (XfceTaskBar *taskbar)
{
    char rc_path [256] ;
    GList *gi ;
    XfceRc *rc; 
    
    
    sprintf(rc_path, "%s/.config/xfce4/panel/taskbar", getenv("HOME"));
    if(!xfce_mkdirhier(rc_path, 0700, NULL))
    {
        xfce_dialog_show_error (NULL, NULL, "TaskBar plugin failed to create the rc config directory");
        return ;
    }
    
    sprintf(rc_path, "%s/.config/xfce4/panel/taskbar/taskbar.rc", getenv("HOME"));
    
    //flush the previous resouce file...
    remove(rc_path);
    rc = xfce_rc_simple_open (rc_path, FALSE);
    if(!rc)
    {
        xfce_dialog_show_error (NULL, NULL, "TaskBar plugin failed to create a configuration file");
        return ;
    }
    
    for(gi=taskbar->wgroups; gi!=NULL; gi=gi->next)
    {
        XfceTaskBarGroup *group =gi->data ;
        if(group->pinned == FALSE)
            continue ;
        
        xfce_rc_set_group (rc, group->window_class_name);
        xfce_rc_write_entry (rc, "command",  group->command);
        
        // Now save the icon in the cache
        {
            char png_path [512] ;
            sprintf(png_path, "%s/.config/xfce4/panel/taskbar/%s.png", getenv("HOME"), group->window_class_name);
            if(!gdk_pixbuf_save (group->pixbuf, png_path, "png", NULL, NULL))
            {
                xfce_dialog_show_error (NULL, NULL, "TaskBar plugin failed to save the icon pixbuf!");
                return ;
            }
        }
    }
    xfce_rc_close (rc);
}

static void xfce_taskbar_finalize (GObject *object)
{
    g_debug("taskbar finalize");
    XfceTaskBar *taskbar = XFCE_taskbar (object);

    // data that should already be freed when disconnecting the screen
    panel_return_if_fail (taskbar->wgroups == NULL);
    panel_return_if_fail (taskbar->skipped_windows == NULL);
    panel_return_if_fail (taskbar->screen == NULL);

    // stop pending timeouts
    if (taskbar->update_icon_geometries_id != 0)
    {
        g_source_remove (taskbar->update_icon_geometries_id);
    }
    if (taskbar->update_monitor_geometry_id != 0)
    {
        g_source_remove (taskbar->update_monitor_geometry_id);
    }

    // free the class group hash table
    g_hash_table_destroy (taskbar->groups);
    
#ifdef GDK_WINDOWING_X11
    // destroy the wireframe window
    xfce_taskbar_wireframe_destroy (taskbar);
#endif
    if(taskbar->hotkeys_handler)
        finish_global_hotkeys(taskbar->hotkeys_handler);

    (*G_OBJECT_CLASS (xfce_taskbar_parent_class)->finalize) (object);
}

static void xfce_taskbar_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
    XfceTaskBar *taskbar = XFCE_taskbar (widget);
    gint rows, cols;
    gint n_group_icons;
    GtkRequisition child_req;
    gint length;
    GList *li;
    XfceTaskBarGroup *group;
    gint child_height = 0;

    for (li = taskbar->wgroups, n_group_icons = 0; li != NULL; li = li->next)
    {
        group = li->data;
        if (GTK_WIDGET_VISIBLE (group->button))
        {
            gtk_widget_size_request (group->button, &child_req);
            child_height = MAX (child_height, child_req.height);
            n_group_icons++;
        }
    }

    taskbar->n_group_icons = n_group_icons;

    if (n_group_icons == 0)
    {
        length = 0;
    }
    else
    {
        rows = taskbar->size / taskbar->max_button_size;
        rows = CLAMP (rows, 1, n_group_icons);

        cols = n_group_icons / rows;
        if (cols * rows < n_group_icons)
            cols++;

        length = (taskbar->size / rows) * cols;
    }

    /* set the requested sizes */
    if (xfce_taskbar_horizontal (taskbar))
    {
        if (taskbar->horizontal != xfce_taskbar_horizontal (taskbar))
        {
            requisition->height = child_height * n_group_icons;
            requisition->width = taskbar->size;
        }
        else
        {
            requisition->width = length;
            requisition->height = taskbar->size;
        }
    }
    else
    {
        requisition->width = taskbar->size;
        requisition->height = length;
    }
}

static void xfce_taskbar_size_layout (XfceTaskBar *taskbar, GtkAllocation *alloc, gint *n_rows, gint *n_cols)
{
    gint rows;
    gint min_button_length;
    gint cols;

    // if we're in the opposite vertical mode, there are no columns
    if (taskbar->horizontal != xfce_taskbar_horizontal (taskbar))
        rows = taskbar->n_group_icons;
    else
        rows = alloc->height / taskbar->max_button_size;

    if (rows < 1)
        rows = 1;

    cols = taskbar->n_group_icons / rows;
    if (cols * rows < taskbar->n_group_icons)
        cols++;

    min_button_length = alloc->height / rows;

    // jam all the icons in, lets see what happens
    *n_rows = rows;
    *n_cols = cols;
}

static void xfce_taskbar_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
    XfceTaskBar *taskbar = XFCE_taskbar (widget);
    gint rows, cols;
    gint row;
    GtkAllocation area = *allocation;
    GList *li;
    XfceTaskBarGroup *group;
    gint i;
    GtkAllocation child_alloc;
    gboolean direction_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
    gint w, x, y, h;
    gint area_x, area_width;
    GtkRequisition child_req;

    //set widget allocation
    widget->allocation = *allocation;

    // swap integers with vertical orientation
    if (!xfce_taskbar_horizontal (taskbar))
        TRANSPOSE_AREA (area);

    // TODO if we compare the allocation with the requisition we can
    // do a fast path to the child allocation, i think

    // useless but hides compiler warning
    w = h = x = y = rows = cols = 0;

    xfce_taskbar_size_layout (taskbar, &area, &rows, &cols);

    // allocate the arrow button for the overflow menu
    child_alloc.width = area.height;
    child_alloc.height = area.height;

    child_alloc.x = child_alloc.y = -9999;

    area_x = area.x;
    area_width = area.width;

    // allocate all the children
    for (li = taskbar->wgroups, i = 0; li != NULL; li = li->next)
    {
        group = li->data;

        // skip hidden buttons
        if (!GTK_WIDGET_VISIBLE (group->button))
            continue;
        
        row = (i % rows);

        if (row == 0)
        {
            x = area_x;
            y = area.y;
            h = area.height;

            w = h / (rows - row);

            area_width -= w;
            area_x += w;
        }

        child_alloc.y = y;
        child_alloc.x = x;
        child_alloc.width = MAX (w, 1); // TODO this is a workaround
        child_alloc.height = h / (rows - row);

        if (!taskbar->horizontal && xfce_taskbar_horizontal (taskbar))
        {
            gtk_widget_get_child_requisition (group->button, &child_req);
            child_alloc.height = child_req.height;
        }

        h -= child_alloc.height;
        y += child_alloc.height;

        if (direction_rtl)
            child_alloc.x = area.x + area.width - (child_alloc.x - area.x) - child_alloc.width;

        // allocate the group
        if (!xfce_taskbar_horizontal (taskbar))
            TRANSPOSE_AREA (child_alloc);

        // increase the position counter
        i++;
    
        gtk_widget_size_allocate (group->button, &child_alloc);
    }

    // update icon geometries
    if (taskbar->update_icon_geometries_id == 0)
    {
        taskbar->update_icon_geometries_id = g_idle_add_full(G_PRIORITY_LOW, xfce_taskbar_update_icon_geometries, 
                          taskbar, xfce_taskbar_update_icon_geometries_destroyed);
    }
}

static void xfce_taskbar_style_set (GtkWidget *widget, GtkStyle    *previous_style)
{
    XfceTaskBar *taskbar = XFCE_taskbar (widget);
    gint max_button_length;
    gint max_button_size;
    gint min_button_length;
    gint w, h;

    // let gtk update the widget style
    (*GTK_WIDGET_CLASS (xfce_taskbar_parent_class)->style_set) (widget, previous_style);

    // read the style properties
    gtk_widget_style_get (GTK_WIDGET (taskbar),
                   "max-button-length", &max_button_length,
                   "min-button-length", &min_button_length,
                   "ellipsize-mode", &taskbar->ellipsize_mode,
                   "max-button-size", &max_button_size,
                   "minimized-icon-lucency", &taskbar->minimized_icon_lucency,
                   "menu-max-width-chars", &taskbar->menu_max_width_chars,
                   NULL);

    if (gtk_icon_size_lookup (menu_icon_size, &w, &h))
        taskbar->menu_icon_size = MIN (w, h);

    // update the widget
    if (taskbar->max_button_length != max_button_length
            || taskbar->max_button_size != max_button_size
            || taskbar->min_button_length != min_button_length)
    {
        if (max_button_length > 0)
        {
            // prevent abuse of the min/max button length
            taskbar->max_button_length = MAX (min_button_length, max_button_length);
            taskbar->min_button_length = MIN (min_button_length, max_button_length);
        }
        else
        {
            taskbar->max_button_length = max_button_length;
            taskbar->min_button_length = min_button_length;
        }

        taskbar->max_button_size = max_button_size;

        gtk_widget_queue_resize (widget);
    }
}

static void xfce_taskbar_realize (GtkWidget *widget)
{
    XfceTaskBar *taskbar = XFCE_taskbar (widget);
    (*GTK_WIDGET_CLASS (xfce_taskbar_parent_class)->realize) (widget);
    xfce_taskbar_connect_screen (taskbar);
}

static void xfce_taskbar_unrealize (GtkWidget *widget)
{
    XfceTaskBar *taskbar = XFCE_taskbar (widget);
    xfce_taskbar_disconnect_screen (taskbar);
    (*GTK_WIDGET_CLASS (xfce_taskbar_parent_class)->unrealize) (widget);
}

// we handle the memory frees elsewhere..
static void xfce_taskbar_remove (GtkContainer *container, GtkWidget *widget)
{
    XfceTaskBar *taskbar = XFCE_taskbar (container);
    gtk_widget_queue_resize (GTK_WIDGET (container));
}

static void xfce_taskbar_forall (GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
    XfceTaskBar *taskbar = XFCE_taskbar (container);
    GList *children = taskbar->wgroups;
    XfceTaskBarGroup *group;

    while (children != NULL)
    {
        group = children->data;
        children = children->next;
        (* callback) (group->button, callback_data);
    }
}

static GType xfce_taskbar_child_type (GtkContainer *container)
{
    return GTK_TYPE_WIDGET;
}

static void xfce_taskbar_connect_screen (XfceTaskBar *taskbar)
{
    GList *windows, *li;

    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == NULL);
    panel_return_if_fail (taskbar->gdk_screen == NULL);

    // set the new screen
    taskbar->gdk_screen = gtk_widget_get_screen (GTK_WIDGET (taskbar));
    taskbar->screen = wnck_screen_get (gdk_screen_get_number (taskbar->gdk_screen));

    // initialize global hotkeys
    taskbar->hotkeys_handler = init_global_hotkeys(taskbar);
    
    
    // add all existing windows on this screen
    windows = wnck_screen_get_windows (taskbar->screen);
    for (li = windows; li != NULL; li = li->next)
    {
        xfce_taskbar_window_added (taskbar->screen, li->data, taskbar);
    }

    // load the pinned items
    xfce_taskbar_load_pinned_config (taskbar);

    // monitor gdk changes
    g_signal_connect (G_OBJECT (taskbar->gdk_screen), "monitors-changed", G_CALLBACK (xfce_taskbar_gdk_screen_changed), taskbar);
    g_signal_connect (G_OBJECT (taskbar->gdk_screen), "size-changed", G_CALLBACK (xfce_taskbar_gdk_screen_changed), taskbar);
    // monitor screen changes
    g_signal_connect (G_OBJECT (taskbar->screen), "active-window-changed", G_CALLBACK (xfce_taskbar_active_window_changed), taskbar);
    g_signal_connect (G_OBJECT (taskbar->screen), "active-workspace-changed", G_CALLBACK (xfce_taskbar_active_workspace_changed), taskbar);
    g_signal_connect (G_OBJECT (taskbar->screen), "window-opened", G_CALLBACK (xfce_taskbar_window_added), taskbar);
    g_signal_connect (G_OBJECT (taskbar->screen), "window-closed", G_CALLBACK (xfce_taskbar_window_removed), taskbar);
    g_signal_connect (G_OBJECT (taskbar->screen), "viewports-changed", G_CALLBACK (xfce_taskbar_viewports_changed), taskbar);

    // update the viewport if not all monitors are shown
    xfce_taskbar_gdk_screen_changed (taskbar->gdk_screen, taskbar);
}

static void xfce_taskbar_disconnect_screen (XfceTaskBar *taskbar)
{
    GSList *li, *lnext;
    GList *wi, *wnext;
    XfceTaskBarGroup *group;
    guint n;

    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (WNCK_IS_SCREEN (taskbar->screen));
    panel_return_if_fail (GDK_IS_SCREEN (taskbar->gdk_screen));
    
    // disconnect monitor signals
    n = g_signal_handlers_disconnect_matched (G_OBJECT (taskbar->screen), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, taskbar);
    panel_return_if_fail (n == 5);

    // disconnect geometry changed signals
    g_signal_handlers_disconnect_by_func (G_OBJECT (taskbar->gdk_screen), G_CALLBACK (xfce_taskbar_gdk_screen_changed), taskbar);

    // delete all known class groups (and their buttons)
    g_hash_table_remove_all (taskbar->groups);

    // disconnect from all skipped windows
    for (li = taskbar->skipped_windows; li != NULL; li = lnext)
    {
        lnext = li->next;
        panel_return_if_fail (wnck_window_is_skip_tasklist (WNCK_WINDOW (li->data)));
        xfce_taskbar_window_removed (taskbar->screen, li->data, taskbar);
    }
    
    for (wi = taskbar->wgroups; wi != NULL; wi = wnext)
    {
        wnext = wi->next;
        group = wi->data;
        xfce_taskbar_group_button_remove(group);
    }
    
    panel_assert (taskbar->wgroups == NULL);
    panel_assert (taskbar->skipped_windows == NULL);
    
    taskbar->screen = NULL;
    taskbar->gdk_screen = NULL;
}

static void xfce_taskbar_gdk_screen_changed (GdkScreen *gdk_screen, XfceTaskBar *taskbar)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (GDK_IS_SCREEN (gdk_screen));
    panel_return_if_fail (taskbar->gdk_screen == gdk_screen);

    if (!taskbar->all_monitors)
    {
        // update the monitor geometry
        xfce_taskbar_update_monitor_geometry (taskbar);
    }
}

static void xfce_taskbar_active_window_changed (WnckScreen *screen, WnckWindow *previous_window, XfceTaskBar *taskbar)
{
    WnckWindow *active_window;
    XfceTaskBarGroup *group;
    GList *li;
    
    panel_return_if_fail (WNCK_IS_SCREEN (screen));
    panel_return_if_fail (previous_window == NULL || WNCK_IS_WINDOW (previous_window));
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == screen);
    
    // get the new active window
    active_window = wnck_screen_get_active_window (screen);
    
    // lock the taskbar
    xfce_taskbar_lock (taskbar);
    
    for (li = taskbar->wgroups; li != NULL; li = li->next)
    {
        GSList *wi;
        XfceTaskBarWNode *wnode ;
        group = li->data;
        
        if (!GTK_WIDGET_VISIBLE (group->button))
        {
            continue ;
        }
        gboolean iconActive=FALSE ;
        for(wi = group->wnodes; wi != NULL; wi = wi->next)
        {
            wnode = wi->data ;
            if(wnode->window == active_window)
            {
                iconActive = TRUE ;
		wnode->timestamp = g_get_real_time();
                break ;
            }
        }
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (group->button), iconActive);
    }
    // release the lock
    xfce_taskbar_unlock (taskbar);
}

static void xfce_taskbar_active_workspace_changed (WnckScreen *screen, WnckWorkspace *previous_workspace, XfceTaskBar *taskbar)
{
    GList *gi;
    WnckWorkspace *active_ws;

    panel_return_if_fail (WNCK_IS_SCREEN (screen));
    panel_return_if_fail (previous_workspace == NULL || WNCK_IS_WORKSPACE (previous_workspace));
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == screen);

    // leave when we are locked or show all workspaces. the null
    // check for @previous_workspace is used to update the taskbar
    // on setting changes
    if (xfce_taskbar_is_locked (taskbar) || (previous_workspace != NULL && taskbar->all_workspaces))
        return;

    // walk all the group buttons and check their visibility
    active_ws = wnck_screen_get_active_workspace (screen);
    for (gi = taskbar->wgroups; gi != NULL; gi = gi->next)
    {
        XfceTaskBarGroup *group;
        group = gi->data;
        
        if(xfce_taskbar_group_visible_count(group, active_ws) == 0 && group->pinned == FALSE)
            gtk_widget_hide(group->button);
        else
            gtk_widget_show(group->button);
    }
    
}

static void xfce_taskbar_window_added (WnckScreen *screen, WnckWindow *window, XfceTaskBar *taskbar)
{
    XfceTaskBarWNode *wnode;
    XfceTaskBarGroup *group = NULL;

    panel_return_if_fail (WNCK_IS_SCREEN (screen));
    panel_return_if_fail (WNCK_IS_WINDOW (window));
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == screen);
    panel_return_if_fail (wnck_window_get_screen (window) == screen);
    panel_return_if_fail (wnck_window_get_application(window) != NULL);
    
    // ignore this window, but watch it for state changes
    if (wnck_window_is_skip_tasklist (window))
    {
        taskbar->skipped_windows = g_slist_prepend (taskbar->skipped_windows, window);
        g_signal_connect (G_OBJECT (window), "state-changed", G_CALLBACK (xfce_taskbar_skipped_windows_state_changed), taskbar);
        return;
    }

    // create new window button
    wnode = xfce_taskbar_wnode_new (window, taskbar);
    
    g_hash_table_lookup_extended (taskbar->groups, wnode->group_name, NULL, (gpointer *) &group);

    if (group == NULL)
    {
        // create group button for this window and add it
        group = xfce_taskbar_group_button_new (wnode->group_name, taskbar);
        
        group->pixbuf = wnck_window_get_icon (window);
        g_object_ref(group->pixbuf);
        
        xfce_panel_image_set_from_pixbuf (XFCE_PANEL_IMAGE (group->icon), group->pixbuf);
        g_hash_table_insert (taskbar->groups, (gpointer)group->window_class_name, group);
    }
    
    xfce_arrow_button_set_blinking(XFCE_ARROW_BUTTON (group->button), FALSE);
    
    wnode->group = group ;
    
    // add window to the group button
    xfce_taskbar_group_button_add_window (group, wnode);

    gtk_widget_queue_resize (GTK_WIDGET (taskbar));
}

static void xfce_taskbar_window_removed (WnckScreen *screen, WnckWindow *window, XfceTaskBar *taskbar)
{
    GList *li;
    GSList *lp;
    XfceTaskBarGroup *group;

    panel_return_if_fail (WNCK_IS_SCREEN (screen));
    panel_return_if_fail (WNCK_IS_WINDOW (window));
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == screen);

    // check if the window is in our skipped window list
    if (wnck_window_is_skip_tasklist (window) && (lp = g_slist_find (taskbar->skipped_windows, window)) != NULL)
    {
        taskbar->skipped_windows = g_slist_delete_link (taskbar->skipped_windows, lp);
        g_signal_handlers_disconnect_by_func (G_OBJECT (window), G_CALLBACK (xfce_taskbar_skipped_windows_state_changed), taskbar);
        return;
    }
    
    for (li = taskbar->wgroups; li != NULL; li = li->next)
    {
        GSList *wi;
        group = li->data;
        
        for(wi = group->wnodes; wi != NULL; wi = wi->next)
        {
            XfceTaskBarWNode *wnode ;
            wnode = wi->data ;
            if (wnode->window != window)
                continue ;
            
            //flush the wireframe (if needed)
            xfce_taskbar_wireframe_hide (taskbar);
            
            //flush the memory associated with the wnode
            xfce_taskbar_wnode_del(wnode);
            
            //actually remove the wnode
            group->wnodes = g_slist_delete_link (group->wnodes, wi);
            goto exit_loop ;
        }
    }
    exit_loop:
    
    if(g_slist_length(group->wnodes) == 0 && group->pinned == FALSE)
    {
        xfce_taskbar_group_button_remove(group);
    }
    else
    {
        //update group button visibility
        xfce_taskbar_group_update_visibility(group);
    }
    
}

static void xfce_taskbar_viewports_changed (WnckScreen *screen, XfceTaskBar *taskbar)
{
    WnckWorkspace *active_ws;

    panel_return_if_fail (WNCK_IS_SCREEN (screen));
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->screen == screen);

    /* pretend we changed workspace, this will update the
     * visibility of all the buttons */
    active_ws = wnck_screen_get_active_workspace (screen);
    xfce_taskbar_active_workspace_changed (screen, active_ws, taskbar);
}

static void xfce_taskbar_skipped_windows_state_changed (WnckWindow *window, WnckWindowState changed_state, WnckWindowState new_state, XfceTaskBar *taskbar)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (WNCK_IS_WINDOW (window));
    panel_return_if_fail (g_slist_find (taskbar->skipped_windows, window) != NULL);

    if (PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_SKIP_TASKLIST))
    {
        /* remove from list */
        taskbar->skipped_windows = g_slist_remove (taskbar->skipped_windows, window);
        g_signal_handlers_disconnect_by_func (G_OBJECT (window),
                G_CALLBACK (xfce_taskbar_skipped_windows_state_changed), taskbar);

        /* pretend a normal window insert */
        xfce_taskbar_window_added (wnck_window_get_screen (window), window, taskbar);
    }
}

static gboolean xfce_taskbar_update_icon_geometries (gpointer data)
{
    XfceTaskBar *taskbar = XFCE_taskbar (data);
    GList *li;
    XfceTaskBarGroup *group;
    XfceTaskBarWNode *wnode;
    GtkAllocation *alloc;
    GSList *lp;
    gint root_x, root_y;
    GtkWidget *toplevel;

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (taskbar));
    gtk_window_get_position (GTK_WINDOW (toplevel), &root_x, &root_y);
    panel_return_val_if_fail (XFCE_IS_taskbar (taskbar), FALSE);

    for (li = taskbar->wgroups; li != NULL; li = li->next)
    {
        group = li->data;

        alloc = &group->button->allocation;
        for (lp = group->wnodes; lp != NULL; lp = lp->next)
        {
            wnode = lp->data;
            panel_return_val_if_fail (WNCK_IS_WINDOW (wnode->window), FALSE);
            wnck_window_set_icon_geometry (wnode->window, alloc->x + root_x, alloc->y + root_y, alloc->width, alloc->height);
        }
    }

    return FALSE;
}

static void xfce_taskbar_update_icon_geometries_destroyed (gpointer data)
{
    XFCE_taskbar (data)->update_icon_geometries_id = 0;
}

static gboolean xfce_taskbar_update_monitor_geometry_idle (gpointer data)
{
    XfceTaskBar *taskbar = XFCE_taskbar (data);
    GdkScreen *screen;
    gboolean geometry_set = FALSE;
    GdkWindow *window;

    panel_return_val_if_fail (XFCE_IS_taskbar (taskbar), FALSE);

    GDK_THREADS_ENTER ();

    if (!taskbar->all_monitors)
    {
        screen = gtk_widget_get_screen (GTK_WIDGET (taskbar));
        window = gtk_widget_get_window (GTK_WIDGET (taskbar));

        if (G_LIKELY (screen != NULL && window != NULL && gdk_screen_get_n_monitors (screen) > 1))
        {
            /* set the monitor geometry */
            gdk_screen_get_monitor_geometry (screen, gdk_screen_get_monitor_at_window (screen, window), &taskbar->monitor_geometry);
            geometry_set = TRUE;
        }
    }

    /* make sure we never poke the window geometry unneeded
     * in the visibility function */
    if (!geometry_set)
    {
        xfce_taskbar_geometry_set_invalid (taskbar);
    }

    /* update visibility of buttons */
    if (taskbar->screen != NULL)
    {
        xfce_taskbar_active_workspace_changed (taskbar->screen, NULL, taskbar);
    }

    GDK_THREADS_LEAVE ();

    return FALSE;
}

static void xfce_taskbar_update_monitor_geometry_idle_destroy (gpointer data)
{
    XFCE_taskbar (data)->update_monitor_geometry_id = 0;
}

#ifdef GDK_WINDOWING_X11
static void xfce_taskbar_wireframe_hide (XfceTaskBar *taskbar)
{
    GdkDisplay *dpy;
    
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    
    if (taskbar->wireframe_window != 0)
    {
        dpy = gtk_widget_get_display (GTK_WIDGET (taskbar));
        XUnmapWindow (GDK_DISPLAY_XDISPLAY (dpy), taskbar->wireframe_window);
    }
}

static void xfce_taskbar_wireframe_destroy (XfceTaskBar *taskbar)
{
    GdkDisplay *dpy;

    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    if (taskbar->wireframe_window != 0)
        {
            /* unmap and destroy the window */
            dpy = gtk_widget_get_display (GTK_WIDGET (taskbar));
            XUnmapWindow (GDK_DISPLAY_XDISPLAY (dpy), taskbar->wireframe_window);
            XDestroyWindow (GDK_DISPLAY_XDISPLAY (dpy), taskbar->wireframe_window);

            taskbar->wireframe_window = 0;
        }
}


static void xfce_taskbar_wireframe_update (XfceTaskBar *taskbar, XfceTaskBarWNode *child)
{
    Display *dpy;
    GdkDisplay *gdpy;
    gint x, y, width, height;
    XSetWindowAttributes attrs;
    GC gc;
    XRectangle xrect;

    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (taskbar->show_wireframes == TRUE);
    panel_return_if_fail (WNCK_IS_WINDOW (child->window));

    /* get the window geometry */
    wnck_window_get_geometry (child->window, &x, &y, &width, &height);

    gdpy = gtk_widget_get_display (GTK_WIDGET (taskbar));
    dpy = GDK_DISPLAY_XDISPLAY (gdpy);

    if (G_LIKELY (taskbar->wireframe_window != 0))
    {
        /* reposition the wireframe */
        XMoveResizeWindow (dpy, taskbar->wireframe_window, x, y, width, height);

        /* full window rectangle */
        xrect.x = 0;
        xrect.y = 0;
        xrect.width = width;
        xrect.height = height;

        /* we need to restore the window first */
        XShapeCombineRectangles (dpy, taskbar->wireframe_window, ShapeBounding,
                            0, 0, &xrect, 1, ShapeSet, Unsorted);
    }
    else
    {
        /* set window attributes */
        attrs.override_redirect = True;
        attrs.background_pixel = 0x000000;

        /* create new window */
        taskbar->wireframe_window = XCreateWindow (dpy, DefaultRootWindow (dpy),
         x, y, width, height, 0,
         CopyFromParent, InputOutput,
         CopyFromParent,
         CWOverrideRedirect | CWBackPixel,
         &attrs);
    }

    /* create rectangle what will be 'transparent' in the window */
    xrect.x = WIREFRAME_SIZE;
    xrect.y = WIREFRAME_SIZE;
    xrect.width = width - WIREFRAME_SIZE * 2;
    xrect.height = height - WIREFRAME_SIZE * 2;

    /* substruct rectangle from the window */
    XShapeCombineRectangles (dpy, taskbar->wireframe_window, ShapeBounding, 0, 0, &xrect, 1, ShapeSubtract, Unsorted);

    /* map the window */
    XMapWindow (dpy, taskbar->wireframe_window);

    /* create a white gc */
    gc = XCreateGC (dpy, taskbar->wireframe_window, 0, NULL);
    XSetForeground (dpy, gc, 0xffffff);

    /* draw the outer white rectangle */
    XDrawRectangle (dpy, taskbar->wireframe_window, gc, 0, 0, width - 1, height - 1);

    /* draw the inner white rectangle */
    XDrawRectangle (dpy, taskbar->wireframe_window, gc, WIREFRAME_SIZE - 1, WIREFRAME_SIZE - 1, width - 2 * (WIREFRAME_SIZE - 1) - 1, height - 2 * (WIREFRAME_SIZE - 1) - 1);

    XFreeGC (dpy, gc);
}
#endif

/**
 * taskbar Buttons
 **/
static inline gboolean xfce_taskbar_button_visible (XfceTaskBarWNode *child, WnckWorkspace *active_ws)
{
    XfceTaskBar *taskbar = XFCE_taskbar (child->taskbar);
    gint x, y, w, h;

    panel_return_val_if_fail (active_ws == NULL || WNCK_IS_WORKSPACE (active_ws), FALSE);
    panel_return_val_if_fail (XFCE_IS_taskbar (taskbar), FALSE);
    panel_return_val_if_fail (WNCK_IS_WINDOW (child->window), FALSE);

    if (xfce_taskbar_filter_monitors (taskbar))
    {
        /* center of the window must be on this screen */
        wnck_window_get_geometry (child->window, &x, &y, &w, &h);
        x += w / 2;
        y += h / 2;

        if (!xfce_taskbar_geometry_has_point (taskbar, x, y))
            return FALSE;
    }

    if (taskbar->all_workspaces || (active_ws != NULL    &&
         (G_UNLIKELY (wnck_workspace_is_virtual (active_ws)) ?
            wnck_window_is_in_viewport (child->window, active_ws)    : wnck_window_is_on_workspace (child->window, active_ws))))
    {
        return (!taskbar->only_minimized || wnck_window_is_minimized (child->window));
    }

    return FALSE;
}

static void xfce_taskbar_button_icon_changed (WnckWindow *window, XfceTaskBarWNode *child)
{
    GdkPixbuf *pixbuf;
    GdkPixbuf *lucent = NULL;
    XfceTaskBar *taskbar = child->taskbar;

    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    panel_return_if_fail (XFCE_IS_PANEL_IMAGE (child->icon));
    panel_return_if_fail (WNCK_IS_WINDOW (window));
    panel_return_if_fail (child->window == window);

    /* 0 means icons are disabled */
    if (taskbar->minimized_icon_lucency == 0)
        return;

    pixbuf = wnck_window_get_icon (window);

    /* leave when there is no valid pixbuf */
    if (G_UNLIKELY (pixbuf == NULL))
    {
        xfce_panel_image_clear (XFCE_PANEL_IMAGE (child->icon));
        return;
    }

    /* create a spotlight version of the icon when minimized */
    if (!taskbar->only_minimized
            && taskbar->minimized_icon_lucency < 100
            && wnck_window_is_minimized (window))
    {
        lucent = exo_gdk_pixbuf_lucent (pixbuf, taskbar->minimized_icon_lucency);
        if (G_UNLIKELY (lucent != NULL))
            pixbuf = lucent;
    }

    xfce_panel_image_set_from_pixbuf (XFCE_PANEL_IMAGE (child->icon), pixbuf);

    if (lucent != NULL && lucent != pixbuf)
        g_object_unref (G_OBJECT (lucent));
}

static void xfce_taskbar_button_name_changed(WnckWindow *window, XfceTaskBarWNode *child)
{
    const gchar *name;
    gchar *label = NULL;

    panel_return_if_fail (window == NULL || child->window == window);
    panel_return_if_fail (WNCK_IS_WINDOW (child->window));
    panel_return_if_fail (XFCE_IS_taskbar (child->taskbar));

    name = wnck_window_get_name (child->window);

    /* create the button label */
    if (!child->taskbar->only_minimized && wnck_window_is_minimized (child->window))
        name = label = g_strdup_printf ("[%s]", name);
    else if (wnck_window_is_shaded (child->window))
        name = label = g_strdup_printf ("=%s=", name);

    gtk_label_set_text (GTK_LABEL (child->label), name);

    g_free (label);
}

static void xfce_taskbar_button_state_changed(WnckWindow *window, WnckWindowState changed_state, WnckWindowState new_state, XfceTaskBarWNode *wnode)
{
    gboolean blink;
    WnckScreen *screen;
    XfceTaskBar *taskbar;

    panel_return_if_fail (WNCK_IS_WINDOW (window));
    panel_return_if_fail (wnode->window == window);
    panel_return_if_fail (XFCE_IS_taskbar (wnode->taskbar));

    /* remove if the new state is hidding the window from the taskbar */
    if (PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_SKIP_TASKLIST))
    {
        screen = wnck_window_get_screen (window);
        taskbar = wnode->taskbar;

        /* remove button from taskbar */
        xfce_taskbar_window_removed (screen, window, wnode->taskbar);

        /* add the window to the skipped_windows list */
        xfce_taskbar_window_added (screen, window, taskbar);

        return;
    }

    /* update the button name */
    if (PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_SHADED | WNCK_WINDOW_STATE_MINIMIZED) && !wnode->taskbar->only_minimized)
    {
        xfce_taskbar_button_name_changed (window, wnode);
    }

    /* update the button icon if needed */
    if (PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_MINIMIZED))
    {
        xfce_taskbar_button_icon_changed (window, wnode);
    }

    /* update the blinking state */
    if (PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_DEMANDS_ATTENTION) || PANEL_HAS_FLAG (changed_state, WNCK_WINDOW_STATE_URGENT))
    {
        /* only start blinking if the window requesting urgency notification is not the active window */
        blink = wnck_window_or_transient_needs_attention (window);
        if (!blink || (blink && !wnck_window_is_active (window)))
        {
            xfce_arrow_button_set_blinking (XFCE_ARROW_BUTTON (wnode->group->button), blink);
        }
    }
}



static void xfce_taskbar_button_workspace_changed (WnckWindow *window, XfceTaskBarWNode *child)
{
    XfceTaskBar *taskbar = XFCE_taskbar (child->taskbar);

    panel_return_if_fail (child->window == window);
    panel_return_if_fail (XFCE_IS_taskbar (child->taskbar));

    /* make sure we don't have two active windows (bug #6474) */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (child->group->button), FALSE);

    if (!taskbar->all_workspaces)
    {
        xfce_taskbar_active_workspace_changed (taskbar->screen, NULL, taskbar);
    }
}



static void xfce_taskbar_button_geometry_changed2 (WnckWindow *window, XfceTaskBarWNode *child)
{
    WnckWorkspace *active_ws;

    panel_return_if_fail (child->window == window);
    panel_return_if_fail (XFCE_IS_taskbar (child->taskbar));
    panel_return_if_fail (WNCK_IS_SCREEN (child->taskbar->screen));

    if (xfce_taskbar_filter_monitors (child->taskbar))
    {
        /* check if we need to change the visibility of the button */
        active_ws = wnck_screen_get_active_workspace (child->taskbar->screen);
        child->visible = xfce_taskbar_button_visible (child, active_ws);
    }
}

#ifdef GDK_WINDOWING_X11
static void xfce_taskbar_button_geometry_changed (WnckWindow *window, XfceTaskBarWNode *child)
{
    panel_return_if_fail (child->window == window);
    panel_return_if_fail (XFCE_IS_taskbar (child->taskbar));

    xfce_taskbar_wireframe_update (child->taskbar, child);
}

static gboolean xfce_taskbar_button_leave_notify_event (GtkWidget *button, GdkEventCrossing *event, XfceTaskBarWNode *child)
{
    g_debug("leave_notify");
    panel_return_val_if_fail (XFCE_IS_taskbar (child->taskbar), FALSE);

    /* disconnect signals */
    g_signal_handlers_disconnect_by_func (button, xfce_taskbar_button_leave_notify_event, child);
    g_signal_handlers_disconnect_by_func (child->window, xfce_taskbar_button_geometry_changed, child);

    /* unmap and destroy the wireframe window */
    xfce_taskbar_wireframe_hide (child->taskbar);

    return FALSE;
}
#endif

static gboolean xfce_taskbar_button_enter_notify_event (GtkWidget *button, GdkEventCrossing *event, XfceTaskBarWNode *child)
{
    panel_return_val_if_fail (XFCE_IS_taskbar (child->taskbar), FALSE);
    panel_return_val_if_fail (GTK_IS_WIDGET (button), FALSE);
    panel_return_val_if_fail (WNCK_IS_WINDOW (child->window), FALSE);

#ifdef GDK_WINDOWING_X11
    /* leave when there is nothing to do */
    if (!child->taskbar->show_wireframes)
        return FALSE;

    /* show wireframe for the child */
    xfce_taskbar_wireframe_update (child->taskbar, child);

    /* connect signal to destroy the window when the user leaves the button */
    g_signal_connect (G_OBJECT (button), "leave-notify-event", G_CALLBACK (xfce_taskbar_button_leave_notify_event), child);

    /* watch geometry changes */
    g_signal_connect (G_OBJECT (child->window), "geometry-changed", G_CALLBACK (xfce_taskbar_button_geometry_changed), child);
#endif

    return FALSE;
}

static void wnck_action_menu_destroy(GtkWidget *wnck_widget, GtkWidget *menu_widget)
{
    g_debug("wnck_action_menu_destroy");
    gtk_widget_destroy(wnck_widget);
    xfce_taskbar_group_button_menu_destroy(menu_widget, NULL);
}

//This function handles mouse release events on the sub-menu of active windows associated with an application group
//Returning FALSE will close the menu that generated the event, return TRUE will persist it
static gboolean xfce_taskbar_app_button_release_event (GtkWidget *button, GdkEventButton *event, XfceTaskBarWNode *child)
{
    g_debug("xfce_taskbar_app_button_release_event");
    panel_return_val_if_fail (XFCE_IS_taskbar (child->taskbar), FALSE);

    /* only respond to in-button events */
    if (
        ((event->type == GDK_BUTTON_RELEASE &&
        !xfce_taskbar_is_locked (child->taskbar) &&
        !(event->x == 0 && event->y == 0) && /* 0,0 = outside the widget in Gtk */
        event->x >= 0 && event->x < button->allocation.width &&
        event->y >= 0 && event->y < button->allocation.height))
        == FALSE
        )
    {
        return FALSE ;
    }
    
    //Sanity check the buttons
    if(event->button < LEFTMOUSE || event->button > RIGHTMOUSE)
        return FALSE ;
    
    if(event->button == LEFTMOUSE)
    {
        xfce_taskbar_button_activate (child, event->time);
        return FALSE ;
    }
    else if(event->button == MIDMOUSE)
    {
        if(WNCK_IS_WINDOW (child->window) == TRUE)
        {
            wnck_window_close (child->window, gtk_get_current_event_time ());
            return FALSE ;
        }
    }
    else// if(event->button == RIGHTMOUSE)
    {
        GtkWidget *menu = wnck_action_menu_new (child->window);
        
        GList *attachlist = gtk_menu_get_for_attach_widget(child->group->button);
        //This submenu is a component of the hover menu
        panel_assert(g_list_length(attachlist) == 1);
        GtkWidget *hover_menu = (GtkWidget *)attachlist->data;
        //Unfortunately, this is quite elaborate :|
        panel_assert(g_signal_handlers_disconnect_by_func (GTK_WINDOW (GTK_MENU(hover_menu)->toplevel), xfce_taskbar_hover_menu_leave, hover_menu) == 1);
        g_signal_connect (G_OBJECT (menu), "selection-done", G_CALLBACK (wnck_action_menu_destroy), hover_menu);
        gtk_menu_attach_to_widget (GTK_MENU (menu), button, NULL);
        gtk_menu_popup (GTK_MENU (menu), NULL, NULL, xfce_panel_plugin_position_menu, xfce_taskbar_get_panel_plugin (child->taskbar), event->button, event->time);
        return TRUE ;
    }
    
    return FALSE ;
}

static void xfce_taskbar_button_enter_notify_event_disconnected (gpointer data, GClosure *closure)
{
    XfceTaskBarWNode *child = data;

    panel_return_if_fail (WNCK_IS_WINDOW (child->window));

    /* we need to detach the geometry watch because that is connected
     * to the window we proxy and thus not disconnected when the
     * proxy dies */
    g_signal_handlers_disconnect_by_func (child->window, xfce_taskbar_button_geometry_changed, child);

    g_object_unref (G_OBJECT (child->window));
}

static GtkWidget *xfce_taskbar_button_proxy_menu_item (XfceTaskBarWNode *child, gboolean allow_wireframe)
{
    GtkWidget *mi;
    GtkWidget *image;
    GtkWidget *label;
    XfceTaskBar *taskbar = child->taskbar;

    panel_return_val_if_fail (XFCE_IS_taskbar (child->taskbar), NULL);
    panel_return_val_if_fail (GTK_IS_LABEL (child->label), NULL);
    panel_return_val_if_fail (WNCK_IS_WINDOW (child->window), NULL);

    mi = gtk_image_menu_item_new ();
    exo_binding_new (G_OBJECT (child->label), "label", G_OBJECT (mi), "label");
    exo_binding_new (G_OBJECT (child->label), "label", G_OBJECT (mi), "tooltip-text");

    label = gtk_bin_get_child (GTK_BIN (mi));
    panel_return_val_if_fail (GTK_IS_LABEL (label), NULL);
    gtk_label_set_max_width_chars (GTK_LABEL (label), taskbar->menu_max_width_chars);
    gtk_label_set_ellipsize (GTK_LABEL (label), taskbar->ellipsize_mode);

    if (G_LIKELY (taskbar->menu_icon_size > 0))
    {
        image = xfce_panel_image_new ();
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
        xfce_panel_image_set_size (XFCE_PANEL_IMAGE (image), taskbar->menu_icon_size);
        exo_binding_new (G_OBJECT (child->icon), "pixbuf", G_OBJECT (image), "pixbuf");
        gtk_widget_show (image);
    }

    if (allow_wireframe)
    {
        g_object_ref (G_OBJECT (child->window));
        g_signal_connect_data (G_OBJECT (mi), "enter-notify-event",
                G_CALLBACK (xfce_taskbar_button_enter_notify_event), child,
                xfce_taskbar_button_enter_notify_event_disconnected, 0);
    }
    
    g_signal_connect (G_OBJECT (mi), "button-release-event", G_CALLBACK (xfce_taskbar_app_button_release_event), child);
    
    return mi;
}

void xfce_taskbar_selgrp_cmd(XfceTaskBar *taskbar, char id) {
    XfceTaskBarGroup *group = g_list_nth_data(taskbar->wgroups, id-1);
    if(!group)
        return;
    XfceTaskBarWNode *first = g_slist_nth_data(group->wnodes, 0);
    if(!first) {
    	// No instances are running
        xfce_taskbar_group_button_launch_pinned(group);
	return;
    }
    // Trying to find an instance with greater timestamp
    GSList *wi ;
    for(wi=group->wnodes; wi != NULL; wi=wi->next) {
	XfceTaskBarWNode *wnode = wi->data;
    	if(wnode->timestamp > first->timestamp)
		first = wnode;
    }
    if (!wnck_window_is_active(first->window)) {
        xfce_taskbar_button_activate(first, 0);
    }
}

static void xfce_taskbar_button_activate (XfceTaskBarWNode *wnode, guint32 timestamp)
{
    WnckWorkspace *workspace;
    gint window_x, window_y;
    gint workspace_width, workspace_height;
    gint screen_width, screen_height;
    gint viewport_x, viewport_y;

    panel_return_if_fail (XFCE_IS_taskbar (wnode->taskbar));
    panel_return_if_fail (WNCK_IS_WINDOW (wnode->window));
    panel_return_if_fail (WNCK_IS_SCREEN (wnode->taskbar->screen));

    if (wnck_window_is_active (wnode->window)
            || wnck_window_transient_is_most_recently_activated (wnode->window))
    {
        wnck_window_minimize (wnode->window);
    }
    else
    {
        // we only change worksapces/viewports for non-pinned windows
        // and if all workspaces/viewports are shown
        if (wnode->taskbar->all_workspaces && !wnck_window_is_pinned (wnode->window))
        {
            workspace = wnck_window_get_workspace (wnode->window);
            // only switch workspaces/viewports if switch_workspace is enabled or
            // we want to restore a minimized window to the current workspace/viewport
            if (workspace != NULL && (wnode->taskbar->switch_workspace || !wnck_window_is_minimized (wnode->window)))
            {
                if (G_UNLIKELY (wnck_workspace_is_virtual (workspace)))
                {
                    if (!wnck_window_is_in_viewport (wnode->window, workspace))
                    {
                        // viewport info
                        workspace_width = wnck_workspace_get_width (workspace);
                        workspace_height = wnck_workspace_get_height (workspace);
                        screen_width = wnck_screen_get_width (wnode->taskbar->screen);
                        screen_height = wnck_screen_get_height (wnode->taskbar->screen);

                        // we only support multiple viewports like compiz has
                        // (all equally spread across the screen)
                        if ((workspace_width % screen_width) == 0 && (workspace_height % screen_height) == 0)
                        {
                           wnck_window_get_geometry (wnode->window, &window_x, &window_y, NULL, NULL);

                           // lookup nearest workspace edge
                           viewport_x = window_x - (window_x % screen_width);
                           viewport_x = CLAMP (viewport_x, 0, workspace_width - screen_width);

                           viewport_y = window_y - (window_y % screen_height);
                           viewport_y = CLAMP (viewport_y, 0, workspace_height - screen_height);

                           // move to the other viewport
                           wnck_screen_move_viewport (wnode->taskbar->screen, viewport_x, viewport_y);
                        }
                        else
                        {
                            g_warning ("only viewport with equally distributed screens are supported: %dx%d & %dx%d", workspace_width, workspace_height, screen_width, screen_height);
                        }
                    }
                }
                else if (wnck_screen_get_active_workspace (wnode->taskbar->screen) != workspace)
                {
                    // switch to the other workspace before we activate the window
                    wnck_workspace_activate (workspace, timestamp - 1);
                }
            }
            else if (workspace != NULL && wnck_workspace_is_virtual (workspace) && !wnck_window_is_in_viewport (wnode->window, workspace))
            {
                // viewport info
                workspace_width = wnck_workspace_get_width (workspace);
                workspace_height = wnck_workspace_get_height (workspace);
                screen_width = wnck_screen_get_width (wnode->taskbar->screen);
                screen_height = wnck_screen_get_height (wnode->taskbar->screen);

                // we only support multiple viewports like compiz has
                // (all equaly spread across the screen)
                if ((workspace_width % screen_width) == 0 && (workspace_height % screen_height) == 0)
                {
                   viewport_x = wnck_workspace_get_viewport_x (workspace);
                   viewport_y = wnck_workspace_get_viewport_y (workspace);

                   // note that the x and y might be negative numbers, since they are relative
                   // to the current screen, not to the edge of the screen they are on. this is
                   // not a problem since the mod result will always be positive
                   wnck_window_get_geometry (wnode->window, &window_x, &window_y, NULL, NULL);

                   // get the new screen position, with the same screen offset
                   window_x = viewport_x + (window_x % screen_width);
                   window_y = viewport_y + (window_y % screen_height);

                   // move the window
                   wnck_window_set_geometry (wnode->window, WNCK_WINDOW_GRAVITY_CURRENT, WNCK_WINDOW_CHANGE_X | WNCK_WINDOW_CHANGE_Y, window_x, window_y, -1, -1);
                }
                else
                {
                    g_warning ("only viewport with equally distributed screens are supported: %dx%d & %dx%d", workspace_width, workspace_height, screen_width, screen_height);
                }
            }
        }
        wnck_window_activate_transient (wnode->window, timestamp);
    }
}

static void xfce_taskbar_button_drag_data_get(GtkWidget *button, GdkDragContext *context, GtkSelectionData *s_data, guint i, guint t, XfceTaskBarGroup *group)
{
    gtk_selection_data_set (s_data, 0, 8, (void *)(&group->unique_id), sizeof (guint));
}

static void xfce_taskbar_button_drag_begin (GtkWidget *button, GdkDragContext *context, XfceTaskBarGroup *group)
{
    // Gruesomely hacky workaround for DnD crashes :(
    usleep(20000);
    group->taskbar->dragactive = TRUE ;
    gtk_drag_set_icon_pixmap(context, gtk_widget_get_colormap(group->icon), gtk_widget_get_snapshot(group->icon, NULL), NULL, 0, 0);
}

static void xfce_taskbar_button_drag_data_received
(GtkWidget *button, GdkDragContext *context, gint x, gint y,
GtkSelectionData *s_data, guint info, guint drag_time, XfceTaskBarGroup *child2)
{
    GList *li, *sibling;
    guint unique_id ;
    XfceTaskBarGroup *child;
    XfceTaskBar *taskbar = XFCE_taskbar (child2->taskbar);
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    
    sibling = g_list_find (taskbar->wgroups, child2);
    panel_return_if_fail (sibling != NULL);

    if ((taskbar->horizontal && x >= button->allocation.width / 2) || (!taskbar->horizontal && y >= button->allocation.height / 2))
    {
        sibling = g_list_next (sibling);
    }

    unique_id = *(guint *) gtk_selection_data_get_data (s_data);
    
    for (li = taskbar->wgroups; li != NULL; li = li->next)
        {
            child = li->data;
            
            if (sibling != li //drop on end previous button
                    && child != child2 //drop on the same button
                    && g_list_next (li) != sibling //drop start of next button
                    && child->unique_id == unique_id)
                {
                    // swap items
                    taskbar->wgroups = g_list_delete_link (taskbar->wgroups, li);
                    taskbar->wgroups = g_list_insert_before (taskbar->wgroups, sibling, child);
                    gtk_widget_queue_resize (GTK_WIDGET (taskbar));
                    break;
                }
        }
    gtk_drag_finish(context, TRUE, FALSE, drag_time) ;
    xfce_taskbar_save_pinned_config(taskbar);
}

static void xfce_taskbar_wnode_del (XfceTaskBarWNode *wnode)
{
    g_signal_handlers_disconnect_matched (G_OBJECT (wnode->window), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, wnode);
    gtk_widget_destroy (wnode->icon);
    gtk_widget_destroy (wnode->label);
    g_free(wnode->group_name);
    g_slice_free (XfceTaskBarWNode, wnode);
}

static XfceTaskBarWNode * xfce_taskbar_wnode_new (WnckWindow *window, XfceTaskBar *taskbar)
{
    WnckWorkspace *active_ws;
    XfceTaskBarWNode *wnode;

    panel_return_val_if_fail (XFCE_IS_taskbar (taskbar), NULL);
    panel_return_val_if_fail (WNCK_IS_WINDOW (window), NULL);

    active_ws = wnck_screen_get_active_workspace (taskbar->screen);

    wnode = g_slice_new0 (XfceTaskBarWNode);
    wnode->taskbar = taskbar;
    wnode->icon = xfce_panel_image_new ();
    wnode->label = gtk_label_new (NULL);
    wnode->window = window;
    wnode->group_name = g_strdup(wnck_application_get_name(wnck_window_get_application (window)));
    wnode->visible = xfce_taskbar_button_visible(wnode, active_ws);
    wnode->timestamp = g_get_real_time();
    
    // monitor window changes
    g_signal_connect (G_OBJECT (window), "icon-changed", G_CALLBACK (xfce_taskbar_button_icon_changed), wnode);
    g_signal_connect (G_OBJECT (window), "name-changed", G_CALLBACK (xfce_taskbar_button_name_changed), wnode);
    g_signal_connect (G_OBJECT (window), "state-changed", G_CALLBACK (xfce_taskbar_button_state_changed), wnode);
    g_signal_connect (G_OBJECT (window), "workspace-changed", G_CALLBACK (xfce_taskbar_button_workspace_changed), wnode);
    g_signal_connect (G_OBJECT (window), "geometry-changed", G_CALLBACK (xfce_taskbar_button_geometry_changed2), wnode);

    // poke functions
    xfce_taskbar_button_icon_changed (window, wnode);
    xfce_taskbar_button_name_changed (NULL, wnode);

    return wnode;
}

static void xfce_taskbar_group_button_menu_minimize_all (XfceTaskBarGroup *group)
{
    GSList *li;
    XfceTaskBarWNode *child;

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        if (child->visible)
        {
            panel_return_if_fail (WNCK_IS_WINDOW (child->window));
            wnck_window_minimize (child->window);
        }
    }
}

static void xfce_taskbar_group_button_menu_unminimize_all (XfceTaskBarGroup *group)
{
    GSList *li;
    XfceTaskBarWNode *child;

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        if (child->visible)
        {
            panel_return_if_fail (WNCK_IS_WINDOW (child->window));
            wnck_window_unminimize (child->window, gtk_get_current_event_time ());
        }
    }
}

static void xfce_taskbar_group_button_menu_maximize_all (XfceTaskBarGroup *group)
{
    GSList *li;
    XfceTaskBarWNode *child;

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        if (child->visible)
        {
            panel_return_if_fail (WNCK_IS_WINDOW (child->window));
            wnck_window_maximize (child->window);
        }
    }
}

static void xfce_taskbar_group_button_menu_unmaximize_all (XfceTaskBarGroup *group)
{
    GSList *li;
    XfceTaskBarWNode *child;

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        if (child->visible)
        {
            panel_return_if_fail (WNCK_IS_WINDOW (child->window));
            wnck_window_unmaximize (child->window);
        }
    }
}

static void xfce_taskbar_group_button_menu_close_all (XfceTaskBarGroup *group)
{
    GSList *li;
    XfceTaskBarWNode *child;

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        panel_return_if_fail (WNCK_IS_WINDOW (child->window));
        wnck_window_close (child->window, gtk_get_current_event_time ());
    }

}

static void xfce_taskbar_group_button_build_pin_menu(XfceTaskBarGroup *group, GtkWidget *menu)
{
    GtkWidget *mi ;
    
    mi = gtk_separator_menu_item_new ();
    gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
    gtk_widget_show (mi);
    
    mi = gtk_image_menu_item_new_with_label ("Pin this program to the taskbar");
    gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_toggle_pinned), group);
    gtk_widget_show (mi);
}

static void xfce_taskbar_group_button_build_launch_menu(XfceTaskBarGroup *group, GtkWidget *menu, gboolean use_sep)
{
    GtkWidget *mi ;
    
    if(use_sep)
    {
        mi = gtk_separator_menu_item_new ();
        gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
        gtk_widget_show (mi);
    }
    mi = gtk_image_menu_item_new_with_label ("Unpin this program from the taskbar");
    gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_toggle_pinned), group);
    gtk_widget_show (mi);
    
    mi = gtk_separator_menu_item_new ();
    gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
    gtk_widget_show (mi);
    
    mi = gtk_image_menu_item_new_with_label ("Launch the application");
    gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_launch_pinned), group);
    gtk_widget_show (mi);
}

static GtkWidget * xfce_taskbar_group_button_menu_launcher(XfceTaskBarGroup *group)
{
    GtkWidget *menu;
    GtkWidget *mi;
    menu = gtk_menu_new ();
    xfce_taskbar_group_button_build_launch_menu(group, menu, FALSE);
    return menu ;
}

// Helper function for building the list of windows associated with a group icon
static GtkWidget * xfce_taskbar_group_button_menu_show_active(XfceTaskBarGroup *group)
{
    GSList *li;
    GtkWidget *menu;

    menu = gtk_menu_new ();

    for (li = group->wnodes; li != NULL; li = li->next)
    {
        XfceTaskBarWNode *wnode ;
        wnode = li->data;
        
        if(wnode->visible)
        {
            GtkWidget *mi;
            mi = xfce_taskbar_button_proxy_menu_item (wnode, TRUE);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
            gtk_widget_show (mi);
        }
    }
    
    return menu ;
}

// Copied most of this code from the verve plugin ;]
static void xfce_taskbar_group_button_launch_pinned (XfceTaskBarGroup *group)
{
    gint         argc;
    gchar      **argv;
    gboolean     success;
    GError      *error = NULL;
    const gchar *home_dir;
    GSpawnFlags  flags;

    panel_return_if_fail((group->pinned == TRUE));

    // Parse command line arguments
    success = g_shell_parse_argv (group->command, &argc, &argv, &error);

    // Return false if command line arguments failed to be parsed
    if (G_UNLIKELY (error != NULL))
    {
        xfce_dialog_show_error (NULL, error, _("Failed to parse the command \"%s\""), group->command);
        g_error_free (error);
        return ;
    }

    // Get user's home directory
    home_dir = xfce_get_homedir ();

    // Set up spawn flags
    flags = G_SPAWN_STDOUT_TO_DEV_NULL;
    flags |= G_SPAWN_STDERR_TO_DEV_NULL;
    flags |= G_SPAWN_SEARCH_PATH;

    // Spawn subprocess
    success = g_spawn_async (home_dir, argv, NULL, flags, NULL, NULL, NULL, &error);
    g_strfreev (argv);
    if(!success)
    {
        xfce_dialog_show_error (NULL, error, _("Failed to execute command \"%s\""), group->command);
        g_error_free (error);
        return ;
    }
    
    //
    //We should only start blinking if it's the first window
    if(g_slist_length(group->wnodes) == 0)
    {
        xfce_arrow_button_set_blinking (XFCE_ARROW_BUTTON (group->button), TRUE);
    }
}

static void xfce_taskbar_group_button_toggle_pinned (XfceTaskBarGroup *group)
{
    if(group->pinned == TRUE)
    {
        panel_return_if_fail((group->command != NULL));
        free(group->command);
        group->command = NULL ;
        group->pinned = FALSE;
        xfce_taskbar_group_update_visibility(group);
    }
    else
    {
        int pid ;
        XfceTaskBarWNode *wnode ;
        panel_return_if_fail((group->wnodes != NULL));
        
        wnode = group->wnodes->data;
        
        pid = wnck_window_get_pid(wnode->window);
        {
            char path_buffer [64] ;
            group->command = (char *) (malloc(command_SIZE*sizeof(char)));
            memset(group->command,   0, command_SIZE*sizeof(char));
            sprintf(path_buffer, "/proc/%i/cmdline", pid);
            {
                int bytes_read ;
                FILE *fp ;
                fp = fopen(path_buffer, "r") ;
                panel_return_if_fail(fp != NULL);
                bytes_read = 0 ;
                while(fread(&group->command[bytes_read], 1, 1, fp) && bytes_read < command_SIZE)
                {
                    if(group->command[bytes_read] == 0x0)
                        group->command[bytes_read] = ' ' ;
                    
                    bytes_read += 1 ;
                }
                panel_return_if_fail(bytes_read != command_SIZE);
            }
            
            
            group->pinned = TRUE ;
        }
    }
    
    // Cache the icon and path data for the pinned items
    xfce_taskbar_save_pinned_config (group->taskbar);
}

// Create the pinning functions first, then *append* the group actions to the menu
// This awkward way of constructing the pinned menu needs to be refactored
static GtkWidget * xfce_taskbar_group_button_menu_group_actions (XfceTaskBarGroup *group)
{
    GSList *li;
    GtkWidget *mi;
    GtkWidget *menu;
    GtkWidget *image;
    XfceTaskBarWNode *wnode;
    gint visibility_count = 0 ;
    
    panel_return_val_if_fail (XFCE_IS_taskbar (group->taskbar), NULL);
    
    menu = gtk_menu_new ();
    
    if(group->pinned == TRUE)
    {
        xfce_taskbar_group_button_build_launch_menu(group, menu, TRUE);
    }
    //Only offer to pin windows with detectable pids
    else if(group->wnodes && wnck_window_get_pid(((XfceTaskBarWNode *)group->wnodes->data)->window) != 0)
    {
        xfce_taskbar_group_button_build_pin_menu(group, menu);
    }
    
    //FIXME: When these submenus activate, they cause a leave-notify event on the hover menu which
    //triggers a destroy event after a short timeout, disabling them for the moment.
    /*
    for (li = group->wnodes; li != NULL; li = li->next)
    {
        wnode = li->data;
        if(wnode->visible)
        {
            visibility_count += 1 ;
            mi = xfce_taskbar_button_proxy_menu_item (wnode, FALSE);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
            gtk_widget_show (mi);
            gtk_menu_item_set_submenu (GTK_MENU_ITEM (mi), wnck_action_menu_new (wnode->window));
        }
    }
    mi = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    gtk_widget_show (mi);
    */

    mi = gtk_image_menu_item_new_with_mnemonic (_("Mi_nimize All"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_menu_minimize_all), group);
    gtk_widget_show (mi);
    image = gtk_image_new_from_stock ("wnck-stock-minimize", GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
    gtk_widget_show (image);

    mi =    gtk_image_menu_item_new_with_mnemonic (_("Un_minimize All"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_menu_unminimize_all), group);
    gtk_widget_show (mi);

    mi = gtk_image_menu_item_new_with_mnemonic (_("Ma_ximize All"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_menu_maximize_all), group);
    gtk_widget_show (mi);
    image = gtk_image_new_from_stock ("wnck-stock-maximize", GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
    gtk_widget_show (image);

    mi =    gtk_image_menu_item_new_with_mnemonic (_("_Unmaximize All"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_menu_unmaximize_all), group);
    gtk_widget_show (mi);

    mi = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    gtk_widget_show (mi);

    mi = gtk_image_menu_item_new_with_mnemonic(_("_Close All"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    g_signal_connect_swapped (G_OBJECT (mi), "activate", G_CALLBACK (xfce_taskbar_group_button_menu_close_all), group);
    gtk_widget_show (mi);
    
    image = gtk_image_new_from_stock ("wnck-stock-delete", GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
    gtk_widget_show (image);

    return menu;
}

static void xfce_taskbar_group_button_menu_destroy(GtkWidget *menu_widget, XfceTaskBarGroup *group)
{
    g_debug("xfce_taskbar_group_button_menu_destroy");
    xfce_taskbar_disable_hover_menu_timeout(menu_widget);
    gtk_widget_destroy (menu_widget);
    if(group)
    {
        #ifdef GDK_WINDOWING_X11
        // Removes the wireframe associated with the currently selected window
        if(group) xfce_taskbar_wireframe_hide (group->taskbar);
        #endif
        // The group button doesn't need to be activated anymore
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (group->button), FALSE);
    }
}


// Get the active_child and visible_count of current group
void get_active_child(XfceTaskBarGroup *group, XfceTaskBarWNode **active_child, int *visible_count) {
    XfceTaskBarWNode *child;
    *active_child = NULL;
    *visible_count = 0;
    GSList *li;
    for (li = group->wnodes; li != NULL; li = li->next)
    {
        child = li->data;
        if(child->visible)
        {
            *visible_count += 1 ;
            if(*active_child == 0x0 && *visible_count == 1)
                *active_child = child ;
            else
            {
                *active_child = 0x0 ;
            }
        }
    }
}

static gboolean xfce_taskbar_group_button_press_event
(GtkWidget *button, GdkEventButton *event, XfceTaskBarGroup *group)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (group->button), TRUE);
    XfceTaskBarWNode *active_child;
    int visible_count;
    get_active_child(group, &active_child, &visible_count);
    g_debug("event_button=%d drag_button=%d", event->button, group->taskbar->drag_button);
    if(group->taskbar->drag_button == 1 
    		&& event->button == LEFTMOUSE 
		&& visible_count > 1) {
	// Activating menu with gtk_widget_show is not so good here as
	// it may prevent notify-event from being received.
	// Considering the situation:
	// 1) User configured drag_button to Middle in his settings
	// 2) The user clicks the button and tries to select the menu item
	// with left button holded
        GtkWidget *menu = xfce_taskbar_group_button_menu_show_active (group);
        g_signal_connect (G_OBJECT (menu), "selection-done", G_CALLBACK (xfce_taskbar_group_button_menu_destroy), group);

        gtk_menu_attach_to_widget (GTK_MENU (menu), button, NULL);
        gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
           xfce_panel_plugin_position_menu,
           xfce_taskbar_get_panel_plugin (group->taskbar),
           event->button,
           event->time);
    	xfce_taskbar_disable_hover_menu_timeout(menu);
	return TRUE;
    }
    return FALSE ;
}

// This function handles all the logic for mouse button relase on the taskbar icons
// It's a witches brew of shimmies and hacks
static gboolean xfce_taskbar_group_button_release_event
(GtkWidget *button, GdkEventButton *event, XfceTaskBarGroup *group)
{
    g_debug("xfce_taskbar_group_button_release_event");
    GtkWidget *panel_plugin;
    GtkWidget *menu_widget;
    
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (group->button), FALSE);
    //Disable the timeout, if active
    DISABLE_HOVER_TIMEOUT(group);
    
    if (event->type != GDK_BUTTON_RELEASE || xfce_taskbar_is_locked (group->taskbar))
        return FALSE;
    
    //Some hacky stuff for drag and drop
    if(group->taskbar->dragactive == TRUE)
    {
        group->taskbar->dragactive = FALSE ;
        group->taskbar->dragtimestamp = gtk_get_current_event_time();
        return FALSE ;
    }
    
    //Middle mouse click on the taskbar icon launchs the icon
    if(event->button == MIDMOUSE)
    {
        xfce_taskbar_group_button_launch_pinned(group);
        return TRUE ;
    }
    
    //Get the number of visible windows for the group icon
    //If there's only a single window visible, set active_child
    //otherwise active_child will be NULL, regardless of multiple
    //active windows
    XfceTaskBarWNode *active_child=0x0 ;
    int visible_count=0x0 ;
    get_active_child(group, &active_child, &visible_count);
    
    //Check if there's a hover menu active, if so, dismiss it, but only if it's outside the cooloff period
    //If possible, detect menu_source to reflect the mouse button which invoked the hover menu
    //This is used to re-activate the hover menu when a different mouse button is used
    size_t menu_source = 0 ;
    if(g_list_length(gtk_menu_get_for_attach_widget(group->button)) == 1)
    {
        GtkMenu *menu = (gtk_menu_get_for_attach_widget(group->button))->data;
        gint time_diff = (gtk_get_current_event_time() - group->hover_visible_timestamp);
        menu_source = (size_t)(g_object_get_data(G_OBJECT(menu), "menu-source"));
        //We don't want to dismiss the menu if the user generates a request for it very shortly after it has appeared
        if(active_child == 0x0 && event->button == LEFTMOUSE && time_diff < HOVER_DISPLAY_COOLOFF)
        {
            return TRUE ;
        }
        gtk_widget_destroy (GTK_WIDGET(menu));
    }
    
    if (event->button == LEFTMOUSE || event->button == RIGHTMOUSE)
    {
        // If a single instance of a class window is visible to the taskbar,
        // activate that instance instead of showing the associated group menus,
        // when a left mouse click falls on the icon
        if(active_child && event->button == LEFTMOUSE)
        {
            xfce_taskbar_button_activate (active_child, event->time);
            return TRUE ;
        }
        
        //2nd click of same mouse button means we don't generate a new menu
        if(menu_source == event->button)
        {
            return TRUE ;
        }
        
        if(visible_count == 0)
        {
            panel_return_val_if_fail ((group->pinned == TRUE), FALSE);
            
            if(event->button == LEFTMOUSE)
            {
                xfce_taskbar_group_button_launch_pinned(group);
                return TRUE ;
            }
            else
            {
                menu_widget = xfce_taskbar_group_button_menu_launcher (group);
            }
        }
        else
        {
            // Split the particular menu use cases into separate functions
            if(event->button == LEFTMOUSE)
            {
                menu_widget = xfce_taskbar_group_button_menu_show_active (group);
            }
            else
            {
                menu_widget = xfce_taskbar_group_button_menu_group_actions (group);
            }
        }
        
	if(event->button == RIGHTMOUSE)
        	xfce_taskbar_activate_hover_menu(menu_widget, group, event->button);
        
        return TRUE;
    }

    return FALSE;
}

//This callback gets triggered if the mouse has left the group icon but has not entered the hover menu
static gboolean xfce_taskbar_hover_menu_timeout(gpointer menu_ptr)
{
    g_debug("xfce_taskbar_hover_menu_timeout");
    GtkWidget *menu_widget = (GtkWidget *)menu_ptr;
    gtk_widget_destroy (menu_widget);
    return FALSE ;
}

//Disable any pending timeout associated with the menu widget
static void xfce_taskbar_disable_hover_menu_timeout(GtkWidget *menu_widget)
{
    size_t timeout_id = (size_t)(g_object_get_data(G_OBJECT(menu_widget), "timeout_id"));
    if(timeout_id != 0)
    {
        g_source_remove(timeout_id);
        timeout_id = 0 ;
        g_object_set_data(G_OBJECT(menu_widget), "timeout_id", (void *)timeout_id);
    }
}

//Triggered when mouse focus enters the hover menu
static gboolean xfce_taskbar_hover_menu_enter(GtkWidget *widget, GdkEvent  *event, gpointer menu_ptr)
{
    GtkWidget *menu_widget = (GtkWidget *)menu_ptr;
    xfce_taskbar_disable_hover_menu_timeout(menu_widget);
    return FALSE ;
}

//Triggered when mouse focus leaves the hover menu
static gboolean xfce_taskbar_hover_menu_leave(GtkWidget *widget, GdkEvent  *event, gpointer menu_ptr)
{
    g_debug("xfce_taskbar_hover_menu_leave");
    GtkWidget *menu_widget = (GtkWidget *)menu_ptr;
    //We don't want to kill the hover menu immediately, so we wait a small time
    size_t timeout_id = (size_t)g_timeout_add(300, xfce_taskbar_hover_menu_timeout, menu_widget);
    g_object_set_data(G_OBJECT(menu_widget), "timeout_id", (void *)timeout_id);
    return FALSE ;
}

static void xfce_taskbar_activate_hover_menu(GtkWidget *menu_widget, XfceTaskBarGroup *group, size_t mouse_button)
{
    gtk_menu_attach_to_widget (GTK_MENU (menu_widget), group->button, NULL);
    g_object_set_data(G_OBJECT(menu_widget), "menu-source", (void *)mouse_button);
    g_signal_connect (G_OBJECT (menu_widget), "selection-done", G_CALLBACK (xfce_taskbar_group_button_menu_destroy), group);
    g_signal_connect (GTK_WINDOW (GTK_MENU(menu_widget)->toplevel), "enter-notify-event", G_CALLBACK (xfce_taskbar_hover_menu_enter), menu_widget);
    g_signal_connect (GTK_WINDOW (GTK_MENU(menu_widget)->toplevel), "leave-notify-event", G_CALLBACK (xfce_taskbar_hover_menu_leave), menu_widget);
    
    {
        gint x, y ;
        gboolean push_in=TRUE;
        GtkMenu *menu = GTK_MENU(menu_widget);
        GtkWindow *menu_window = GTK_WINDOW (menu->toplevel);
        
        xfce_panel_plugin_position_menu(menu, &x, &y, &push_in, xfce_taskbar_get_panel_plugin(group->taskbar));
        
        menu->position_func = xfce_panel_plugin_position_menu;
        menu->position_func_data = xfce_taskbar_get_panel_plugin(group->taskbar);
        
        (GTK_MENU_SHELL (menu_widget))->active = TRUE;
        
        gtk_widget_show (menu->toplevel);
        gtk_window_move(menu_window, x, y);
        gtk_widget_show (menu_widget);
    }
}

//Triggered when the mouse has hovered over the group icon for duration of time
static gboolean xfce_taskbar_group_button_hover_timeout(gpointer group_ptr)
{
    GtkWidget *menu_widget;
    XfceTaskBarGroup *group = (XfceTaskBarGroup *)group_ptr ;
    
    
    //Disable the hover timeout
    DISABLE_HOVER_TIMEOUT(group);
    
    //Create and raise the menu
    menu_widget = xfce_taskbar_group_button_menu_show_active (group);
    //This isn;t a proper event, so we cannot use gtk_get_current_event_time();
    //Therefore we set hover_visible_timestamp in xfce_taskbar_group_button_enter_event
    group->hover_visible_timestamp += GROUP_ICON_HOVER_TIMEOUT ;
    
    //Attach the menu widget to the button widget
    if(group->taskbar->show_instances_hover)
    	xfce_taskbar_activate_hover_menu(menu_widget, group, LEFTMOUSE);
    
    return FALSE ;
}

//Triggered when the mouse exits the group button icon
static gboolean xfce_taskbar_group_button_leave_event(GtkWidget *button_widget, GdkEvent *event, XfceTaskBarGroup *group)
{
    DISABLE_HOVER_TIMEOUT(group);
    
    {
        GList *attachlist = gtk_menu_get_for_attach_widget(button_widget);
        guint attachcount = g_list_length(attachlist);
        if(attachcount == 1)
        {
            //We;ve got a hover menu attached, trigger a timeout for destroying it
            GtkWidget *menu_widget = (GtkWidget *)(attachlist->data);
            //We don't want to kill the hover menu immediately, so we wait a small time
            size_t timeout_id = (size_t)g_timeout_add(300, xfce_taskbar_hover_menu_timeout, menu_widget);
            g_object_set_data(G_OBJECT(menu_widget), "timeout_id", (void *)timeout_id);
        }
    }
    
    return FALSE ;
}

//Triggered when the mouse enters the group button icon
static gboolean xfce_taskbar_group_button_enter_event(GtkWidget *button, GdkEvent *e, XfceTaskBarGroup *group)
{
    GdkEventCrossing *event = (GdkEventCrossing *)e ;
    
    if(event->time - group->taskbar->dragtimestamp < 20)
        return TRUE ;
    
    if(xfce_taskbar_group_visible_count(group, wnck_screen_get_active_workspace (group->taskbar->screen)) > 0)
    {
        group->hover_timeout = g_timeout_add(GROUP_ICON_HOVER_TIMEOUT, xfce_taskbar_group_button_hover_timeout, group);
        group->hover_visible_timestamp = gtk_get_current_event_time();
    }
    return TRUE ;
}

static int xfce_taskbar_group_visible_count(XfceTaskBarGroup *group, WnckWorkspace *active_ws)
{
    GSList *wi ;
    int visible_count ;
    visible_count = 0 ;
    for(wi=group->wnodes; wi != NULL; wi=wi->next)
    {
        XfceTaskBarWNode *wnode;
        wnode = wi->data ;
        wnode->visible = xfce_taskbar_button_visible (wnode, active_ws);
        visible_count += !!wnode->visible ;
    }
    return visible_count ;
}

static void xfce_taskbar_group_update_visibility(XfceTaskBarGroup *group)
{
    WnckWorkspace *active_ws;
    
    active_ws = wnck_screen_get_active_workspace (group->taskbar->screen);
    if(xfce_taskbar_group_visible_count(group, active_ws) == 0)
    {
        if(group->pinned == FALSE)
        {
            gtk_widget_hide(group->button);
        }
        else
        {
            gtk_button_set_relief (GTK_BUTTON (group->button), GTK_RELIEF_NONE);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (group->button), 0);
            gtk_widget_show(group->button);
        }
    }
    else
    {
        gtk_button_set_relief (GTK_BUTTON (group->button),
					group->taskbar->flat_buttons
					? GTK_RELIEF_NONE
					: GTK_RELIEF_NORMAL);
        gtk_widget_show(group->button);
    }
    
    gtk_widget_queue_resize (GTK_WIDGET (group->taskbar));
}

static void xfce_taskbar_group_button_remove (XfceTaskBarGroup *group)
{
    GSList *wi;
    GList *gi;
    panel_return_if_fail (XFCE_IS_taskbar (group->taskbar));
    if (group == NULL)
        return;
    
    if(group->pinned == TRUE)
    {
        free(group->command);
        group->pinned = FALSE ;
    }
    
    for(wi = group->wnodes; wi != NULL; wi = wi->next)
    {
        XfceTaskBarWNode *wnode ;
        wnode = wi->data ;
        xfce_taskbar_wnode_del(wnode);
    }
    
    g_slist_free (group->wnodes);
    group->wnodes = NULL;
    
    g_object_unref(group->pixbuf);
    gtk_widget_destroy (group->icon);
    gtk_widget_destroy (group->align);
    gtk_widget_destroy (group->button);
    
    for(gi=group->taskbar->wgroups; gi != NULL; gi = gi->next)
    {
        XfceTaskBarGroup *gnode ;
        gnode = gi->data ;
        if(gnode == group)
        {
            group->taskbar->wgroups = g_list_delete_link(group->taskbar->wgroups, gi);
            g_hash_table_remove(group->taskbar->groups, (gpointer)group->window_class_name);
            g_free(group->window_class_name);
            g_slice_free (XfceTaskBarGroup, group);
            return ;
        }
    }
    
    panel_assert (0x0 && "Failed to remove the task group!");
}

static void xfce_taskbar_group_button_add_window (XfceTaskBarGroup *group, XfceTaskBarWNode *wnode)
{
    panel_return_if_fail (WNCK_IS_WINDOW (wnode->window));
    panel_return_if_fail (strcmp(wnode->group_name, group->window_class_name) == 0);
    panel_return_if_fail (XFCE_IS_taskbar (group->taskbar));
    panel_return_if_fail (g_slist_find (group->wnodes, wnode) == NULL);

    /* add to internal list */
    group->wnodes = g_slist_append (group->wnodes, wnode);

    /* update visibility for the current workspace */
    xfce_taskbar_group_update_visibility(group);
}

static XfceTaskBarGroup * xfce_taskbar_group_button_new (const char *group_name, XfceTaskBar *taskbar)
{
    GdkPixbuf *pixbuf;
    XfceTaskBarGroup *group;
    
    panel_return_val_if_fail (XFCE_IS_taskbar (taskbar), NULL);
    
    group = g_slice_new0 (XfceTaskBarGroup);
    group->taskbar = taskbar;
    group->unique_id = taskbar->unique_id_counter++;
    group->window_class_name = g_strdup(group_name);
    
    
    //Prep the pinning data
    group->pinned = FALSE;
    group->command = NULL ;
    
    //The timeout id, used when tracking a mouse hovering over the button
    group->hover_timeout = 0 ;
    
    //This is a usability tweak, I frequently find that I click the group icon
    //just after the hover menu appears, causing it to be removed, this timestamp
    //will be used to ensure that a small window of time has elapsed before destroy
    //the window based on a user mouse click
    group->hover_visible_timestamp = 0 ;
    
    group->button = xfce_arrow_button_new (GTK_ARROW_NONE);
    gtk_widget_set_parent (group->button, GTK_WIDGET (taskbar));
    gtk_widget_set_tooltip_text (group->button, group_name);
    group->icon = xfce_panel_image_new ();
    group->align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add( GTK_CONTAINER(group->align), group->icon);
    gtk_container_add (GTK_CONTAINER (group->button), group->align);
    
    g_signal_connect (G_OBJECT (group->button), "button-press-event", G_CALLBACK (xfce_taskbar_group_button_press_event), group);
    g_signal_connect (G_OBJECT (group->button), "button-release-event", G_CALLBACK (xfce_taskbar_group_button_release_event), group);
    
    g_signal_connect(G_OBJECT(group->button), "enter-notify-event", G_CALLBACK(xfce_taskbar_group_button_enter_event), group);
    g_signal_connect(G_OBJECT(group->button),"leave-notify-event", G_CALLBACK(xfce_taskbar_group_button_leave_event), group);
    
    /* insert */
    taskbar->wgroups = g_list_append(taskbar->wgroups, group);
    
    //Show the widgets
    gtk_widget_show(group->icon);
    gtk_widget_show(group->align);
    gtk_widget_show(group->button);
    
    /* drag and drop to the pager */
    gtk_drag_dest_set     (group->button, GTK_DEST_DEFAULT_ALL,    source_targets, G_N_ELEMENTS (source_targets), GDK_ACTION_MOVE);
    gtk_drag_source_set (group->button, taskbar->drag_button ? GDK_BUTTON2_MASK : GDK_BUTTON1_MASK,            source_targets, G_N_ELEMENTS (source_targets), GDK_ACTION_MOVE);
    
    g_signal_connect (G_OBJECT (group->button), "drag-data-get",            G_CALLBACK (xfce_taskbar_button_drag_data_get), group);
    g_signal_connect (G_OBJECT (group->button), "drag-begin",                 G_CALLBACK (xfce_taskbar_button_drag_begin), group);
    g_signal_connect (G_OBJECT (group->button), "drag-data-received", G_CALLBACK (xfce_taskbar_button_drag_data_received), group);
    
    return group;
}

/**
 * Potential Public Functions
 **/
static void xfce_taskbar_set_include_all_workspaces (XfceTaskBar *taskbar, gboolean all_workspaces)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    all_workspaces = !!all_workspaces;

    if (taskbar->all_workspaces != all_workspaces)
    {
        taskbar->all_workspaces = all_workspaces;
        if (taskbar->screen != NULL)
        {
            /* update visibility of buttons */
            xfce_taskbar_active_workspace_changed (taskbar->screen, NULL, taskbar);
        }
    }

}

static void xfce_taskbar_set_include_all_monitors (XfceTaskBar *taskbar, gboolean all_monitors)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    all_monitors = !!all_monitors;

    if (taskbar->all_monitors != all_monitors)
        {
            taskbar->all_monitors = all_monitors;

            /* set the geometry to invalid or update the geometry and
             * update the visibility of the buttons */
            if (all_monitors)
            {
                xfce_taskbar_geometry_set_invalid (taskbar);

                /* update visibility of buttons */
                xfce_taskbar_active_workspace_changed (taskbar->screen, NULL, taskbar);
            }
            else if (taskbar->gdk_screen != NULL)
            {
                xfce_taskbar_gdk_screen_changed (taskbar->gdk_screen, taskbar);
            }
        }
}



static void xfce_taskbar_set_show_only_minimized (XfceTaskBar *taskbar, gboolean only_minimized)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    only_minimized = !!only_minimized;

    if (taskbar->only_minimized != only_minimized)
        {
            taskbar->only_minimized = only_minimized;

            /* update all windows */
            if (taskbar->screen != NULL)
                xfce_taskbar_active_workspace_changed (taskbar->screen,
                                      NULL, taskbar);
        }
}



static void xfce_taskbar_set_show_wireframes (XfceTaskBar *taskbar, gboolean show_wireframes)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    taskbar->show_wireframes = !!show_wireframes;

#ifdef GDK_WINDOWING_X11
    /* destroy the window if needed */
    xfce_taskbar_wireframe_destroy (taskbar);
#endif
}


void xfce_taskbar_set_orientation (XfceTaskBar *taskbar, GtkOrientation orientation)
{
    gboolean horizontal;
    
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));
    
    horizontal = !!(orientation == GTK_ORIENTATION_HORIZONTAL);
    if (taskbar->horizontal != horizontal)
    {
        //Setting this variable is important, idle loop?
        taskbar->horizontal = horizontal;
    }
}

void xfce_taskbar_set_size (XfceTaskBar *taskbar, gint size)
{
    panel_return_if_fail (XFCE_IS_taskbar (taskbar));

    if (taskbar->size != size)
    {
        taskbar->size = size;
        gtk_widget_queue_resize (GTK_WIDGET (taskbar));
    }
}

void xfce_taskbar_update_monitor_geometry (XfceTaskBar *taskbar)
{
    if (taskbar->update_monitor_geometry_id == 0)
    {
        taskbar->update_monitor_geometry_id = 
        g_idle_add_full (G_PRIORITY_LOW, xfce_taskbar_update_monitor_geometry_idle, taskbar, xfce_taskbar_update_monitor_geometry_idle_destroy);
    }
}

