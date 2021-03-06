//
// filter_dialog.c
//

// ========================
//
// Creates and shows plug-in dialog window,
// displays preview of tile map/set info
//
// ========================

//#include "config.h"
//#include <string.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "win_aligned_alloc.h"

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <gtk/gtk.h>

#include "filter_tilemap_helper.h"
#include "filter_dialog.h"
#include "scale.h"
#include "lib_tilemap.h"
#include "tilemap_overlay.h"
#include "tilemap_export.h"

#include "benchmark.h"


extern const char PLUG_IN_PROCEDURE[];
extern const char PLUG_IN_ROLE[];
extern const char PLUG_IN_BINARY[];

static int dialog_scaled_preview_check_resize(GtkWidget *, gint, gint, gint);

static void dialog_source_image_free_and_reset(void);
static void dialog_source_image_initialize(void);

static gint dialog_source_image_load(GimpDrawable * drawable);
static gint dialog_source_colormap_load(GimpDrawable * drawable);

static void on_scaled_preview_mouse_exited(GtkWidget * window, gpointer callback_data);
static void on_scaled_preview_mouse_moved(GtkWidget * window, gpointer callback_data);
static void on_scaled_preview_mouse_clicked(GtkWidget * window, gpointer callback_data);

static void on_setting_scale_spinbutton_changed(GtkSpinButton *, gpointer);
static void on_setting_tilesize_spinbutton_changed(GtkSpinButton *, gint);
static void on_setting_overlay_checkbutton_changed(GtkToggleButton *, gpointer);
static void on_setting_finalbpp_combo_changed(GtkComboBox *, gpointer);
static void on_setting_flattened_image_checkbutton_changed(GtkToggleButton *, gpointer);
static void on_setting_checkflip_checkbutton_changed(GtkToggleButton *, gpointer);
static void on_setting_maptoclipboard_type_combo_changed(GtkComboBox *, gpointer);
static void on_setting_setting_maptoclipboard_prefix_entry_changed(GtkEntry *, gpointer);

static void on_action_maptoclipboard_button_clicked(GtkButton *, gpointer);


static void dialog_settings_apply_to_ui(void);
static void dialog_settings_connect_signals(GimpDrawable *);

static void dialog_ui_update(void);
int dialog_calc_dest_bpp(int);

static void info_display_update(void);

static void tilemap_copy_map_to_clipboard(void);

gboolean preview_scaled_update(GtkWidget *, GdkEvent *, GtkWidget *);

static void tilemap_calculate(void);

static void tilemap_render_overlay(void);

static void tilemap_preview_display_tilenum_on_mouseover(gint x, gint y, GtkAllocation widget_alloc);
static void tilemap_preview_highlight_tiles_on_mouseclick(gint x, gint y, GtkAllocation widget_alloc);


const gchar * const finalbpp_strs[]          = { "Src Image", "1", "2", "3", "4", "8", "9", "16", "24", "32"};
const gchar * const srcbpp_str[]             = {" ", "Source: 8", "Source: 16", "Source: 24", "Source: 32"};
const gchar * const srcbpp_dialogtitle_str[] = {" ", "8 bits/pixel, Indexed", "16 bits/pixel, Indexed-A", "24 bits/pixel, RGB", "32 bits/pixel, RGB-A"};

const gchar * const maptoclipboard_type_str[] = {"C Array",          "ASM RGBDS"};
enum export_copy_types                         { EXPORT_COPY_TYPE_C, EXPORT_COPY_TYPE_ASM_RGBDS};

const gchar * const tile_flip_str[]          = { " ", ", flip: X", ", flip: Y", ", flip: X+Y" };



// Widget for displaying the upscaled image preview
static GtkWidget * preview_scaled;
static GtkWidget * scaled_preview_window;
static GtkWidget * tile_info_display;
static GtkWidget * memory_info_display;
static GtkWidget * mouse_hover_display;


// TODO: consider passing parent vbox into widget creation so these can be moved to local vars (?)
static GtkWidget * setting_scale_label;
static GtkWidget * setting_scale_spinbutton;

static GtkWidget * setting_flattened_image_checkbutton;

static GtkWidget * setting_overlay_grid_checkbutton;
static GtkWidget * setting_overlay_tileids_checkbutton;

static GtkWidget * setting_finalbpp_combo;

static GtkWidget * setting_maptoclipboard_type_combo;
static GtkWidget * setting_maptoclipboard_prefix_entry;

static GtkWidget * setting_tilesize_label;
static GtkWidget * setting_tilesize_width_spinbutton;
static GtkWidget * setting_tilesize_height_spinbutton;

static GtkWidget * setting_checkflip_checkbutton;
static GtkWidget * setting_checkrotation_checkbutton;

static GtkWidget * action_maptoclipboard_button;

static PluginTileMapVals dialog_settings;


// TODO: move these out of global scope?
static image_data      app_image;
static color_data      app_colors;

static gint32          image_id;


/*******************************************************/
/*               Main Plug-in Dialog                   */
/*******************************************************/
gint tilemap_dialog_show (GimpDrawable *drawable)
{
    GtkWidget * dialog;
    GtkWidget * main_vbox;

    GtkWidget * setting_table;
    GtkWidget * setting_preview_label;
    GtkWidget * setting_processing_label;

    GtkWidget * setting_tilesize_hbox;

    GtkWidget * setting_finalbpp_label;
    GtkWidget * setting_finalbpp_hbox;

    GtkWidget * maptoclipboard_prefix_label;
    GtkWidget * maptoclipboard_frame;
    GtkWidget * maptoclipboard_hbox;

    GtkWidget * mouse_hover_frame;

    gboolean   run_result;
    gint       idx;
    char       dialog_title_str[255];


    gimp_ui_init (PLUG_IN_BINARY, FALSE);

    // Show source image bits-per-pixel and image mode in the dialog title if possible
    if ((drawable->bpp >=1) && (drawable->bpp < ARRAY_LEN(srcbpp_str)))
        snprintf(dialog_title_str, 255, "Tilemap Helper  - Source Image: %s", srcbpp_dialogtitle_str[drawable->bpp]);
    else
        snprintf(dialog_title_str, 255, "Tilemap Helper");

    dialog = gimp_dialog_new (dialog_title_str, PLUG_IN_ROLE,
                            NULL, 0,
                            gimp_standard_help_func, PLUG_IN_PROCEDURE,

                            GTK_BUTTONS_NONE,

                            NULL);


    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            "Create Tileset Image", GTK_RESPONSE_APPLY,
                            "Cancel",         GTK_RESPONSE_CANCEL,
                            "Save Settings",  GTK_RESPONSE_OK,
                            NULL);

    // Resize to show more of scaled preview by default (this sets MIN size)
    gtk_widget_set_size_request (dialog,
                               650,
                               600);


    gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL,
                                           -1);

    gimp_window_set_transient (GTK_WINDOW (dialog));


    // Create a main vertical box for the preview
    main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      main_vbox, TRUE, TRUE, 0);
    gtk_widget_show (main_vbox);


    // ======== IMAGE PREVIEW ========

    // Create scaled preview area and a scrolled window area to hold it
    // TODO: gimp_drawable_preview_new() is deprecated as of 2.10 -> gimp_drawable_preview_new_from_drawable_id()
    preview_scaled = gimp_preview_area_new();
    scaled_preview_window = gtk_scrolled_window_new (NULL, NULL);


    // Automatic scrollbars for scrolled preview window
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scaled_preview_window),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);


    // Add the scaled preview to the scrolled window
    // and then display them both (with auto-resize)
    gtk_scrolled_window_add_with_viewport((GtkScrolledWindow *)scaled_preview_window,
                                              preview_scaled);
    gtk_box_pack_start (GTK_BOX (main_vbox), scaled_preview_window, TRUE, TRUE, 0);
    gtk_widget_show (scaled_preview_window);
    gtk_widget_show (preview_scaled);


    // Wire up scaled preview redraw to the size-allocate event (Window changed size/etc)
    // resize scaled preview -> destroys scaled preview buffer -> resizes scroll window -> size-allocate -> redraw preview buffer
    // This fixes the scrolled window inhibiting the redraw when the size changed
    g_signal_connect_swapped(preview_scaled,
                             "size-allocate",
                             G_CALLBACK(tilemap_dialog_processing_run), drawable);


    // ======== UI CONTROLS ========

    // Create n x n table for Settings, non-homogonous sizing, attach to main vbox
    // TODO: Consider changing from a table to a grid (tables are deprecated)
    setting_table = gtk_table_new (5, 6, FALSE);
    gtk_box_pack_start (GTK_BOX (main_vbox), setting_table, FALSE, FALSE, 0);
    gtk_table_set_row_spacings(GTK_TABLE(setting_table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(setting_table), 20);
    //gtk_table_set_homogeneous(GTK_TABLE (setting_table), TRUE);
    gtk_table_set_homogeneous(GTK_TABLE (setting_table), FALSE);


    // == Preview Settings ==
    setting_preview_label = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(setting_preview_label), "<b>Preview</b>");
    gtk_misc_set_alignment(GTK_MISC(setting_preview_label), 0.0f, 0.5f); // Left-align

        // Checkboxes for the image overlays
        setting_overlay_grid_checkbutton = gtk_check_button_new_with_label("Grid");
        // gtk_misc_set_alignment(GTK_MISC(setting_overlay_grid_checkbutton), 1.0f, 0.5f); // Right-align

        setting_overlay_tileids_checkbutton = gtk_check_button_new_with_label("Tile IDs");
        // gtk_misc_set_alignment(GTK_MISC(setting_overlay_tileids_checkbutton), 1.0f, 0.5f); // Right-align


        // Spin button for the zoom scale factor
        setting_scale_label = gtk_label_new ("Zoom:" );
        gtk_misc_set_alignment(GTK_MISC(setting_scale_label), 0.0f, 0.5f); // Left-align
        setting_scale_spinbutton = gtk_spin_button_new_with_range(1,10,1); // Min/Max/Step


    // == Processing Settings ==
    setting_processing_label = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(setting_processing_label), "<b>Processing</b>");
    gtk_misc_set_alignment(GTK_MISC(setting_processing_label), 0.0f, 0.5f); // Left-align

        // Create label and right-align it
        setting_tilesize_label = gtk_label_new ("Tile size (w x h):  " );
        gtk_misc_set_alignment(GTK_MISC(setting_tilesize_label), 0.0f, 0.5f); // Left-align
        // Tile width/height widgets
        setting_tilesize_width_spinbutton  = gtk_spin_button_new_with_range(2,256,1); // Min/Max/Step
        setting_tilesize_height_spinbutton = gtk_spin_button_new_with_range(2,256,1); // Min/Max/Step

        // Horizontal box for tile width/height widgets
        // (Forces them to a nicer looking, smaller size)
        setting_tilesize_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_container_set_border_width (GTK_CONTAINER (setting_tilesize_hbox), 3);
        gtk_box_pack_start (GTK_BOX (setting_tilesize_hbox), setting_tilesize_width_spinbutton, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (setting_tilesize_hbox), setting_tilesize_height_spinbutton, FALSE, FALSE, 0);


        // Checkboxes for flipping on tile deduplication
        setting_checkflip_checkbutton = gtk_check_button_new_with_label("Check Flip X/Y");
        // TODO: and rotation...
        setting_checkrotation_checkbutton = gtk_check_button_new_with_label("Check Rotation");

        // Checkbox for whether to sample the source image as a single layer or flattened
        setting_flattened_image_checkbutton = gtk_check_button_new_with_label("Flattened Image");

    // Info readout/display area
    tile_info_display = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(tile_info_display),
                         g_markup_printf_escaped("<b>Tiles:</b>"));
    gtk_label_set_max_width_chars(GTK_LABEL(tile_info_display), 29);
    gtk_label_set_ellipsize(GTK_LABEL(tile_info_display),PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(tile_info_display), 0.0f, 0.0f);

    memory_info_display = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(memory_info_display),
                         g_markup_printf_escaped("<b>Memory:</b>"));
    gtk_label_set_max_width_chars(GTK_LABEL(memory_info_display), 29);
    gtk_label_set_ellipsize(GTK_LABEL(memory_info_display),PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(memory_info_display), 1.0f, 0.0f);
    gtk_label_set_justify(GTK_LABEL(memory_info_display), GTK_JUSTIFY_RIGHT);

        // Combo box to customize the final bits-per-pixel of the tile data
        setting_finalbpp_label = gtk_label_new("Bits-per-pixel: ");
        gtk_misc_set_alignment(GTK_MISC(setting_finalbpp_label), 1.0, 0.5f);

        setting_finalbpp_combo = gtk_combo_box_text_new ();

         // Load first entry: "Source Image" bpp setting with image mode displayed
         if ((drawable->bpp >=1) && (drawable->bpp < ARRAY_LEN(srcbpp_str)))
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setting_finalbpp_combo), srcbpp_str[drawable->bpp]);
        else
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setting_finalbpp_combo), finalbpp_strs[0]);

        // Load the rest of the fixed-value entries from a const array
        for (idx = 1; idx < ARRAY_LEN(finalbpp_strs); idx++)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setting_finalbpp_combo), finalbpp_strs[idx]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(setting_finalbpp_combo), 0);

        // Horizontal box for tile final bits-per-pixel selector and label
        // (Forces them to a smaller size)
        setting_finalbpp_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_container_set_border_width (GTK_CONTAINER (setting_finalbpp_hbox), 0);
        gtk_box_pack_end (GTK_BOX (setting_finalbpp_hbox), setting_finalbpp_combo, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (setting_finalbpp_hbox), setting_finalbpp_label, FALSE, FALSE, 0);



    // Info readout/display area for mouse hover on the scaled preview area
    mouse_hover_display = gtk_label_new (NULL);
    gtk_misc_set_alignment(GTK_MISC(mouse_hover_display), 0.0f, 0.5f); // Left-align
    // Put the label inside a frame
    mouse_hover_frame = gtk_frame_new(NULL);
    gtk_container_add (GTK_CONTAINER (mouse_hover_frame), mouse_hover_display);


    // Put the Export / Copy Map -> Clipboard button and options inside a frame
    maptoclipboard_frame = gtk_frame_new(NULL);//("Export to Clipboard"); // Bug in some GTK themes: Adding a label to a frame hides the border
    maptoclipboard_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width (GTK_CONTAINER (maptoclipboard_hbox), 5);
    gtk_container_add (GTK_CONTAINER (maptoclipboard_frame), maptoclipboard_hbox);

        // Option to specify prefix
        maptoclipboard_prefix_label = gtk_label_new("Export Prefix");
        setting_maptoclipboard_prefix_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(setting_maptoclipboard_prefix_entry), MAP_PREFIX_MAX_LEN);
        gtk_entry_set_alignment (GTK_ENTRY(setting_maptoclipboard_prefix_entry), 1.0);

        // Clipboard copy type (Load the combo box entries from a const array)
        setting_maptoclipboard_type_combo = gtk_combo_box_text_new ();

        for (idx = 0; idx < ARRAY_LEN(maptoclipboard_type_str); idx++)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setting_maptoclipboard_type_combo), maptoclipboard_type_str[idx]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(setting_maptoclipboard_type_combo), 0);

        // Copy to clipboard button
        action_maptoclipboard_button = gtk_button_new_with_label("Copy Map -► Clipboard");

        gtk_box_pack_end (GTK_BOX (maptoclipboard_hbox), action_maptoclipboard_button, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (maptoclipboard_hbox), setting_maptoclipboard_type_combo, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (maptoclipboard_hbox), setting_maptoclipboard_prefix_entry, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (maptoclipboard_hbox), maptoclipboard_prefix_label, FALSE, FALSE, 0);

    // Attach the UI WIdgets to the table and show them all
    // gtk_table_attach_defaults (*attach_to, *widget, left_attach, right_attach, top_attach, bottom_attach)
    //
    gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_preview_label,            0, 2, 0, 1);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_scale_label,          0, 2, 1, 2);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_scale_spinbutton,     0, 1, 2, 3);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_overlay_grid_checkbutton,     0, 2, 3, 4);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_overlay_tileids_checkbutton,  0, 2, 4, 5);

    gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_processing_label,                2, 3, 0, 1);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_tilesize_label,              2, 3, 1, 2);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_tilesize_hbox,               2, 3, 2, 3);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_checkflip_checkbutton,       2, 3, 3, 4);
//        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_checkrotation_checkbutton,   2, 3, 4, 5);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_flattened_image_checkbutton,   2, 3, 4, 5);

    gtk_table_attach_defaults (GTK_TABLE (setting_table), tile_info_display,        3, 4, 0, 4);  // Vertical Column
    gtk_table_attach_defaults (GTK_TABLE (setting_table), memory_info_display,      4, 5, 0, 4);  // Vertical Column
    gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_finalbpp_hbox,    4, 5, 4, 5);  // Bottom right


    // Attach mouse hover info area to bottom of main vbox (below table)
    gtk_box_pack_start (GTK_BOX (main_vbox), mouse_hover_frame, FALSE, FALSE, 0);

    // Attach map to clipboard area to bottom of main vbox
    gtk_box_pack_start (GTK_BOX (main_vbox), maptoclipboard_frame, FALSE, FALSE, 0);

    gtk_widget_show_all (setting_table);

    gtk_widget_show_all (maptoclipboard_frame);

    gtk_widget_show (mouse_hover_frame);
    gtk_widget_show (mouse_hover_display);

    dialog_settings_apply_to_ui();

    dialog_settings_connect_signals(drawable);


    // ======== SHOW THE DIALOG AND RUN IT ========

    // TODO: move this to an init function
    scaled_output_invalidate();
    tilemap_recalc_invalidate();
    overlay_redraw_invalidate();
    dialog_source_image_initialize();


    gtk_widget_show (dialog);
    run_result = gimp_dialog_run (GIMP_DIALOG (dialog));
    gtk_widget_destroy (dialog);

    return run_result;
}



// For calling plugin to set dialog settings, including in headless mode
//
void tilemap_dialog_settings_set(PluginTileMapVals * p_plugin_config_vals) {

    // Copy plugin settings to dialog settings
    memcpy (&dialog_settings, p_plugin_config_vals, sizeof(PluginTileMapVals));
}



// For calling plugin to retrieve dialog settings (to persist for next-run)
//
void tilemap_dialog_settings_get(PluginTileMapVals * p_plugin_config_vals) {

    // Copy dialog settings to plugin settings
    memcpy (p_plugin_config_vals, &dialog_settings, sizeof(PluginTileMapVals));
}



// For calling plugin to set dialog settings, including in headless mode
//
void tilemap_dialog_imageid_set(gint32 new_image_id) {

    // Set local copy of source image id
    image_id = new_image_id;
}






// Load dialog settings into UI (called on startup)
//
void dialog_settings_apply_to_ui(void) {

    gint idx;
    //gchar * compare_str[255];
    gchar incoming_val_str[255];


    //printf("==== Applying Dialog Settings to UI\n");
    // ======== UPDATE WIDGETS TO CURRENT SETTINGS ========

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_scale_spinbutton),           dialog_settings.scale_factor);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_tilesize_width_spinbutton),  dialog_settings.tile_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_tilesize_height_spinbutton), dialog_settings.tile_height);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(setting_overlay_grid_checkbutton),    dialog_settings.overlay_grid_enabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(setting_overlay_tileids_checkbutton), dialog_settings.overlay_tileids_enabled);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(setting_checkflip_checkbutton),       dialog_settings.check_flip);

    gtk_combo_box_set_active(GTK_COMBO_BOX(setting_maptoclipboard_type_combo), dialog_settings.maptoclipboard_type );
    gtk_entry_set_text(GTK_ENTRY(setting_maptoclipboard_prefix_entry), dialog_settings.maptoclipboard_prefix_str );

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(setting_flattened_image_checkbutton), dialog_settings.flattened_image);


    gtk_combo_box_set_active(GTK_COMBO_BOX(setting_finalbpp_combo), 0);

    // Loop through combo options to see if any match the string for the finalbpp value,
    // if there is a match then update combo box selection
    snprintf(incoming_val_str, 255, "%d", dialog_settings.finalbpp);

    for (idx = 0; idx < ARRAY_LEN(finalbpp_strs); idx++)
            if (!(g_strcmp0( incoming_val_str, finalbpp_strs[idx] )))
            gtk_combo_box_set_active(GTK_COMBO_BOX(setting_finalbpp_combo), idx);
}



void dialog_settings_connect_signals(GimpDrawable *drawable) {

    // ======== SCALED PREVIEW MOUSEOVER INFO ========

    // Add mouse movement event
    gtk_widget_add_events(preview_scaled, GDK_POINTER_MOTION_MASK);

    // Connect the mouse moved signal to a display function
    g_signal_connect (preview_scaled, "motion-notify-event",
                      G_CALLBACK (on_scaled_preview_mouse_moved), NULL);

// scaled_preview_window
    gtk_widget_add_events(scaled_preview_window, GDK_BUTTON_PRESS);
    g_signal_connect (scaled_preview_window, "button-press-event",
                      G_CALLBACK (on_scaled_preview_mouse_clicked), NULL);


    // Add event for when the mouse leaves the window (clear info display)
    gtk_widget_add_events(preview_scaled, GDK_LEAVE_NOTIFY_MASK);

    // Connect the mouse moved signal to a display function
    g_signal_connect (preview_scaled, "leave-notify-event",
                      G_CALLBACK (on_scaled_preview_mouse_exited), NULL);

    // ======== HANDLE UI CONTROL VALUE UPDATES ========

    // Connect the changed signal to update the scaler mode
    g_signal_connect (setting_scale_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_scale_spinbutton_changed), NULL);

    // Connect the changed signal to update the scaler mode
    g_signal_connect (setting_tilesize_width_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_tilesize_spinbutton_changed), GINT_TO_POINTER(WIDGET_TILESIZE_WIDTH));
    g_signal_connect (setting_tilesize_height_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_tilesize_spinbutton_changed), GINT_TO_POINTER(WIDGET_TILESIZE_HEIGHT));

    // Flip X/Y updates
    g_signal_connect(G_OBJECT(setting_checkflip_checkbutton), "toggled",
                      G_CALLBACK(on_setting_checkflip_checkbutton_changed), NULL);


    // Overlay control changes (will require a re-render)
    g_signal_connect(G_OBJECT(setting_overlay_grid_checkbutton), "toggled",
                      G_CALLBACK(on_setting_overlay_checkbutton_changed), NULL);
    g_signal_connect(G_OBJECT(setting_overlay_tileids_checkbutton), "toggled",
                      G_CALLBACK(on_setting_overlay_checkbutton_changed), NULL);

    // Final bits-per-pixel change for Memory info readout (just needs a recalc)
    g_signal_connect (setting_finalbpp_combo, "changed",
                      G_CALLBACK (on_setting_finalbpp_combo_changed), NULL);

    g_signal_connect(G_OBJECT(setting_flattened_image_checkbutton), "toggled",
                      G_CALLBACK(on_setting_flattened_image_checkbutton_changed), NULL);

    g_signal_connect (setting_maptoclipboard_type_combo, "changed",
                      G_CALLBACK (on_setting_maptoclipboard_type_combo_changed), NULL);

    g_signal_connect (setting_maptoclipboard_prefix_entry, "changed",
                      G_CALLBACK (on_setting_setting_maptoclipboard_prefix_entry_changed), NULL);


    // ======== HANDLE PROCESSING UPDATES VIA UI CONTROL VALUE CHANGES ========

    // Connect a second signal to trigger a preview update for setting changes
    // This will trigger various different levels of re-processing based on the setting

    // Scaling zoom changed
    g_signal_connect_swapped (setting_scale_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);

    // Tile Size
    g_signal_connect_swapped (setting_tilesize_width_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);
    g_signal_connect_swapped (setting_tilesize_height_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);

    // Flip X/Y
    g_signal_connect_swapped (setting_checkflip_checkbutton, "toggled",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);


    // Overlay options
    g_signal_connect_swapped (setting_overlay_grid_checkbutton, "toggled",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);
    g_signal_connect_swapped (setting_overlay_tileids_checkbutton, "toggled",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);

    // Source image vs Source layer
    g_signal_connect_swapped (setting_flattened_image_checkbutton, "toggled",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);

    // Mouse clicks on the preview image itself
    g_signal_connect_swapped (scaled_preview_window, "button-press-event",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);


    // ======== MISC ACTIONS ========

    // Copy map to clipboard
    g_signal_connect (action_maptoclipboard_button, "clicked",
                      G_CALLBACK (on_action_maptoclipboard_button_clicked), NULL);
}





static void on_scaled_preview_mouse_exited(GtkWidget * widget, gpointer callback_data) {
    // Mouse Tile Display: Outside preview area, clear info
    gtk_label_set_markup(GTK_LABEL(mouse_hover_display),
                     g_markup_printf_escaped(" " ) );
}



static void on_scaled_preview_mouse_moved(GtkWidget * widget, gpointer callback_data) {

    GtkAllocation allocation;
    gint x,y;

    // Note: gtk_widget_get_pointer() is deprecated, eventually use...
    //   -> gdk_window_get_device_position (window, mouse, &x, &y, NULL);
    gtk_widget_get_pointer(widget, &x, &y);
    gtk_widget_get_allocation (widget, &allocation);

    // Display info about the tile the mouse is over
    tilemap_preview_display_tilenum_on_mouseover(x,y, allocation);
}


static void on_scaled_preview_mouse_clicked(GtkWidget * widget, gpointer callback_data) {

    GtkAllocation allocation;
    gint x,y;

    // Note: gtk_widget_get_pointer() is deprecated, eventually use...
    //   -> gdk_window_get_device_position (window, mouse, &x, &y, NULL);
    gtk_widget_get_pointer(preview_scaled, &x, &y);
    gtk_widget_get_allocation (preview_scaled, &allocation);

    // Highlight all tiles that match the one clicked by the mouse
    tilemap_preview_highlight_tiles_on_mouseclick(x,y, allocation);
}


// Handler for "changed" signal of SCALER MODE combo box
// When the user changes the scaler type -> Update the scaler mode
//
//   callback_data not used currently
//
static void on_setting_scale_spinbutton_changed(GtkSpinButton * spinbutton, gpointer callback_data)
{
    //printf("        --> EVENT: on_setting_tilesize_spinbutton_changed\n");

    dialog_settings.scale_factor = gtk_spin_button_get_value_as_int(spinbutton);
}



// TODO: ?consolidate to a single spin button UI update handler?
static void on_setting_tilesize_spinbutton_changed(GtkSpinButton * spinbutton, gint callback_data)
{
    //printf("        --> EVENT: on_setting_tilesize_spinbutton_changed\n");

    switch (callback_data) {
        case WIDGET_TILESIZE_WIDTH:  dialog_settings.tile_width  = gtk_spin_button_get_value_as_int(spinbutton);
            tilemap_recalc_invalidate();
            break;
        case WIDGET_TILESIZE_HEIGHT: dialog_settings.tile_height = gtk_spin_button_get_value_as_int(spinbutton);
            tilemap_recalc_invalidate();
            break;
    }
}


static void on_setting_overlay_checkbutton_changed(GtkToggleButton * p_togglebutton, gpointer callback_data) {
    // Update settings for both checkboxes
    dialog_settings.overlay_grid_enabled    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(setting_overlay_grid_checkbutton));
    dialog_settings.overlay_tileids_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(setting_overlay_tileids_checkbutton));

    // Request a redraw of the scaled preview + overlay
    overlay_redraw_invalidate();
}


static void on_setting_finalbpp_combo_changed(GtkComboBox * combo, gpointer callback_data)
{
    dialog_settings.finalbpp = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo)));

    dialog_ui_update();
}


static void on_setting_maptoclipboard_type_combo_changed(GtkComboBox * combo, gpointer callback_data)
{
    dialog_settings.maptoclipboard_type = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
}


static void on_setting_setting_maptoclipboard_prefix_entry_changed(GtkEntry * entry, gpointer callback_data)
{
  strncpy(dialog_settings.maptoclipboard_prefix_str,
          gtk_entry_get_text(GTK_ENTRY(entry)),
          MAP_PREFIX_MAX_LEN);
}


static void on_setting_flattened_image_checkbutton_changed(GtkToggleButton * p_togglebutton, gpointer callback_data) {

    dialog_settings.flattened_image = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(setting_flattened_image_checkbutton));

    // Request a reload of the source image and then a redraw of everything
    dialog_source_image_free_and_reset();
    scaled_output_invalidate();
    tilemap_recalc_invalidate();
}


static void on_setting_checkflip_checkbutton_changed(GtkToggleButton * p_togglebutton, gpointer callback_data) {

    dialog_settings.check_flip = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(setting_checkflip_checkbutton));

    tilemap_recalc_invalidate();
}


static void on_action_maptoclipboard_button_clicked(GtkButton * button, gpointer callback_data) {
    tilemap_copy_map_to_clipboard();
}



// Checks to see whether the scaled preview area needs
// to be resized. Handles resizing if needed.
//
// Called from pixel_art_scalers_run() which is used for
// previewing and final rendering of the selected scaler mode
//
static int dialog_scaled_preview_check_resize(GtkWidget * preview_scaled, gint width_new, gint height_new, gint scale_factor_new)
{
    gint width_current, height_current;

    // Get current size for scaled preview area
    gtk_widget_get_size_request (preview_scaled, &width_current, &height_current);

    printf("Dialog: Check Window Resize...(%d/%d) (%d/%d)  Resize = ",
        width_current, (width_new  * scale_factor_new),
        height_current, (height_new * scale_factor_new));
    // Only resize if the width, height or scaling changed
    if ( (width_current  != (width_new  * scale_factor_new)) ||
         (height_current != (height_new * scale_factor_new)) )
    {
        // TODO: This queues a second redraw event... it seems to work fine. Does it need to be fixed?
        printf("YES  (applied, queueing another redraw pass...)\n");

        // Resize scaled preview area
        gtk_widget_set_size_request (preview_scaled, width_new * scale_factor_new, height_new * scale_factor_new);

        // when set_size_request and then draw are called repeatedly on a preview_area
        // it causes redraw glitching in the surrounding scrolled_window region
        // Calling set_max_size appears to fix this
        // (though it may be treating the symptom and not the cause of the glitching)
        gimp_preview_area_set_max_size(GIMP_PREVIEW_AREA (preview_scaled),
                                       width_new * scale_factor_new,
                                       height_new * scale_factor_new);
        // Yes, a redraw was queued
        return TRUE;
    }
    else
        printf("NO\n");

    // No redraw queued
    return FALSE;
}



int dialog_calc_dest_bpp(int src_bpp) {

    // If image is INDEXED or INDEXED ALPHA
    // Then promote dest image:
    // * 1 bpp -> RGB 3 bpp,
    // * 2 bpp (alpha) -> RGBA 4bpp

    if (src_bpp <= 2)
        return (3 + (src_bpp - 1));
    else
        return src_bpp;
}

/*******************************************************/
/*   Create the dialog preview image or output layer   */
/*******************************************************/
//
//
// Previews and performs the final output rendering of
// the selected scaler.
//
// Called from:
// * Signals...
//   -> preview_scaled -> size_allocate
//   -> spin buttons -> value-changed
// * The end of this file.c (if user pressed "OK" to apply)
//
void tilemap_dialog_processing_run(GimpDrawable *drawable, GimpPreview  *preview)
{
    GimpImageType drawable_type;
    gint         dest_bpp;

    scaled_output_info * scaled_output;



    printf("Process: Start --> tilemap recalc = %d, scale = %d\n", tilemap_recalc_needed(), dialog_settings.scale_factor);

    // Apply dialog settings
    tilemap_overlay_set_enables(dialog_settings.overlay_grid_enabled,
                                dialog_settings.overlay_tileids_enabled);

    scale_factor_set( dialog_settings.scale_factor );

    // Check for previously rendered output
    scaled_output = scaled_info_get();

    // Load source image data if needed
    if (app_image.p_img_data == NULL)
        dialog_source_image_load(drawable);

    // Check to see if the window size has changed or needs to change
    //
    // If the check triggered a resize then a second redraw will be queued, and
    // so this should exit immediately.--> The rest of the processing and
    // drawing will happen on the second pass
    if (dialog_scaled_preview_check_resize( preview_scaled,
                                            app_image.width, app_image.height,
                                            dialog_settings.scale_factor))
        return;

    // Get bit depth and alpha mask status
    dest_bpp = dialog_calc_dest_bpp(app_image.bytes_per_pixel);


    // Allocate output buffer for upscaled image
    // NOTE: This is feeding in the dest/scaled RGB/ALPHA 3/4 BPP that was promoted from INDEXED/ALPHA 1/2 BPP
    scaled_output_check_reallocate(dest_bpp,
                                   app_image.width,
                                   app_image.height);

    // TODO: switch this to an invalidate model like tilemap_recalc_needed()? - then above could be merged in and nested
    if (scaled_output_check_reapply_scale() || tilemap_recalc_needed()) {

        if (scaled_output_check_reapply_scale()) {
            // ====== APPLY THE SCALER ======
            // NOTE: Promotes INDEXED/ALPHA 1/2 BPP to RGB/ALPHA 3/4 BPP
            //       Expects p_destbuf to be allocated with 3/4 BPP RGB/A number of bytes, not 1/2 if INDEXED
            scale_apply(app_image.p_img_data,
                        scaled_output->p_scaledbuf,
                        app_image.bytes_per_pixel,
                        app_image.width, app_image.height,
                        app_colors.pal, app_colors.color_count,
                        dest_bpp);

            // Queue a tile map overlay redraw since the scaled output changed
            overlay_redraw_invalidate();
        }

        // ====== CALCULATE TILE MAP & TILES ======
        if (tilemap_recalc_needed()) {
            tilemap_calculate();

            tilemap_color_data_set(&app_colors);

            // Queue a tile map overlay redraw since the tile map info changed
            overlay_redraw_invalidate();
        }
    }

    // ====== DRAW OVERLAY FROM TILE MAP & TILES ======
    if (overlay_redraw_needed()) {

        printf("Overlay: Clear buffer...");
        benchmark_start();
        // Copy the upscaled tile buffer to the overlay buffer
        // This allows redrawing the overlay without having to re-scale
        memcpy(scaled_output->p_overlaybuf,
               scaled_output->p_scaledbuf,
               scaled_output->width * scaled_output->height * dest_bpp);
        benchmark_elapsed();

        // Only redraw if there is a valid tilemap to work from, otherise just clear it
        if ( !tilemap_recalc_needed() ) {
            // Note: Drawing of individual overlays controlled via tilemap_overlay_set_enables()
            // For now, every time we change the tile size, we have to re-scale the image
            tilemap_overlay_setparams(//scaled_output->p_scaledbuf,
                                      scaled_output->p_overlaybuf,
                                      dest_bpp,
                                      scaled_output->width,
                                      scaled_output->height,
                                      dialog_settings.tile_width * dialog_settings.scale_factor,
                                      dialog_settings.tile_height * dialog_settings.scale_factor);

            tilemap_render_overlay();
        }
    }
//    else

    // Filter is done, apply the update
    if (preview) {

        printf("PAINT: ? (valid=%d)..  ", scaled_output->valid_image);
        // Redraw the scaled preview if it's available (it ought to be at this point)
        if ( (scaled_output->p_scaledbuf != NULL) &&
             (scaled_output->valid_image == TRUE) ) {

            printf("YES");
            benchmark_start();

            // Select drawable render type based on BPP of upscaled image
            if      (dest_bpp == BPP_RGB)  drawable_type = GIMP_RGB_IMAGE;
            else if (dest_bpp == BPP_RGBA) drawable_type = GIMP_RGBA_IMAGE;
            else     drawable_type = GIMP_RGB_IMAGE; // fallback to RGB // TODO: should handle this better...

            // TODO: move the painting into a function

            // TODO: This rendering is OK, may this may need to be optimized
            //
            // TODO: The preview area also gets super slow when panning around with large upscaled images (~10x)
            //       Seems like it's trying to redraw the entire image on a scroll signal, instead of just the
            //       region visible in the viewport.
            //
            //   GtkScrolledWindow (scroll-child)->GtkViewport->GimpPreviewArea
            //
            //   Get the viewport offset and size (repeat for Horizontal):
            //     GtkAdjustment * window_adjustment_v, * window_adjustment_h;
            //      window_adjustment_v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(preview_scaled_window));
            //     gtk_adjustment_get_value(window_adjustment_v)
            //     gtk_adjustment_get_page_size(window_adjustment_v)
            //
            // OR...
            // 1. Create a drawable from the scaled output
            // 2. Attach that drawable to a scrolled_preview_area handles panning well via gimp_drawable_preview_new()
            // 3. Update the drawable as needed?
            // * Might have trouble detaching the drawable during resizes?... (the api to update it has been removed)


            gimp_preview_area_draw (GIMP_PREVIEW_AREA (preview_scaled),
                                    0, 0,                  // x,y
                                    scaled_output->width,  // width, height
                                    scaled_output->height,
                                    drawable_type,                              // GimpImageType (scaled image) // gimp_drawable_type (drawable->drawable_id),
                                    (guchar *) scaled_output->p_overlaybuf,     // Source buffer
                                    //(guchar *) scaled_output->p_scaledbuf,      // Source buffer
                                    scaled_output->width * scaled_output->bpp); // Row-stride

            benchmark_elapsed();

            printf("PAINT: Blended...");
            benchmark_start();
/*
            gimp_preview_area_blend(
                    GIMP_PREVIEW_AREA (preview_scaled),
                    0, 0,                  // x,y
                    scaled_output->width,  // width, height
                    scaled_output->height,
                    drawable_type,                              // GimpImageType (scaled image) // gimp_drawable_type (drawable->drawable_id),

                    (guchar *) scaled_output->p_scaledbuf,      // Source buffer
                    scaled_output->width * scaled_output->bpp, // Row-stride

                    (guchar *) scaled_output->p_scaledbuf,      // Source buffer
                    scaled_output->width * scaled_output->bpp, // Row-stride
                    255);            // Opacity
*/
            benchmark_elapsed();
        }
        else printf("NO\n");

        // Update the info display area
        // update_text_readout();

    }
    else
    {
/*
        // Apply image result with full resize
        resize_image_and_apply_changes(drawable,
                                       (guchar *) scaled_output->p_scaledbuf,
                                       scaled_output->scale_factor);
*/
    }


    printf("PROCESS: === END ===\n\n");
}


static void dialog_source_image_initialize(void) {

    app_image.p_img_data = NULL;
    app_colors.color_count = 0;
}


static void dialog_source_image_free_and_reset(void) {

    if (app_image.p_img_data)
        free(app_image.p_img_data);

    app_image.p_img_data = NULL;

    app_colors.color_count = 0;
}


// TODO: move this and above into a separate file
static gint dialog_source_image_load(GimpDrawable * drawable_layer) {

    GimpPixelRgn src_rgn;
    gint         width, height;
    gint         x, y;

    gint32         temp_image_id;
    gint32         temp_flattened_layer;
    GimpDrawable * source_drawable;
    size_t         alloc_size;

    printf("Source Image: Loading (flattened=%d)...\n", dialog_settings.flattened_image);

    temp_image_id = 0;

    // SOURCE IMAGE: Use either layer passed to the plugin, or a
    //               flattened copy of the entire image (default)
    if (dialog_settings.flattened_image) {
        // Make a copy of the current image, merge all layers, then retrieve the drawable
        temp_image_id         = gimp_image_duplicate(image_id);
        temp_flattened_layer  = gimp_image_merge_visible_layers(temp_image_id, GIMP_CLIP_TO_IMAGE);
        source_drawable       = gimp_drawable_get(temp_flattened_layer);
    }
       else {
           source_drawable = drawable_layer;
   }

    // gimp_preview_get_position (preview, &x, &y);
    // gimp_preview_get_size (preview, &width, &height);
    if (! gimp_drawable_mask_intersect (source_drawable->drawable_id,
                                        &x, &y, &width, &height)) {
        dialog_source_image_free_and_reset();
        if (temp_image_id)
            gimp_image_delete (temp_image_id);
        return FALSE;
    }

    // Get the Bytes Per Pixel of the incoming app image
    app_image.bytes_per_pixel = source_drawable->bpp;

    // Determine the array size for the app's image then allocate it
    app_image.width      = width;
    app_image.height     = height;
    app_image.size       = app_image.width * app_image.height * app_image.bytes_per_pixel;

    // Source image buffer allocated with 32 bit alignment
    // app_image.p_img_data = (uint8_t *) g_new (guint32, app_image.width * app_image.height);

    // aligned_alloc expects SIZE to be a multiple of ALIGNMENT, so pad with a couple bytes if needed
    alloc_size = app_image.size + (app_image.size % sizeof(uint32_t));
    printf(" (allocating %zu bytes %" PRId32 " %zu) \n", alloc_size, app_image.size, (app_image.size % sizeof(uint32_t)));

    app_image.p_img_data = (uint8_t *)aligned_alloc(sizeof(uint32_t), app_image.size);


    // FALSE, FALSE : region will be used to read the actual drawable data
    // Initialize source pixel region with drawable
    gimp_pixel_rgn_init (&src_rgn,
                         source_drawable,
                         x, y,
                         width, height,
                         FALSE, FALSE);

    // Copy source image to working buffer
    gimp_pixel_rgn_get_rect (&src_rgn,
                             (guchar *) app_image.p_img_data,
                             x, y, width, height);


    if ( !dialog_source_colormap_load(source_drawable) ) {
        dialog_source_image_free_and_reset();
        if (temp_image_id)
            gimp_image_delete (temp_image_id);
        return false;
    }

    printf("Source Image: ... Loading Completed\n");
    if (temp_image_id)
        if (! gimp_image_delete (temp_image_id) ) {
            printf("Source Image: **Warning, failed to delete cloned image**\n");
        }

    return true;
}


static gint dialog_source_colormap_load(GimpDrawable * drawable) {

    guchar     * p_colormap_buf;
    gint         colormap_numcolors;

    colormap_numcolors = 0;
    p_colormap_buf = NULL;

    // TODO: handle grayscale?
    // gimp_drawable_is_gray()
    // gimp_drawable_is_rgb()
    // Load color map if needed
    if (gimp_drawable_is_indexed(drawable->drawable_id)) {

        // Load the color map and copy it to a working buffer
        p_colormap_buf = gimp_image_get_colormap(image_id, &colormap_numcolors);

        // Abort if there are too many colors
        if (colormap_numcolors > COLOR_DATA_PAL_MAX_COUNT)
            return false;

        // Make a local copy of the color map (rgb24 * number of colors)
        memcpy(&(app_colors.pal[0]), p_colormap_buf, colormap_numcolors * 3);
        app_colors.color_count = colormap_numcolors;
        app_colors.size = colormap_numcolors * 3;
    }
    else
        app_colors.color_count = 0;

    return true; // Success
}




// TODO: variable tile size (push down via app settings?)
//  gint image_id, gint drawable_id, gint image_mode)
void tilemap_calculate(void) {

    gint status;

    status = TRUE; // Default to success

    if (tilemap_recalc_needed()) {
        // printf("Tilemap: Starting Recalc: tilemap_recalc_needed() = %d\n\n", tilemap_recalc_needed());
        status = tilemap_export_process(&app_image,
                                        dialog_settings.tile_width,
                                        dialog_settings.tile_height,
                                        dialog_settings.check_flip);

        // TODO: warn/notify on failure (invalid tile size, etc)
      if (!status)
        printf("Tilemap: Recalc -> FAILED: tilemap_recalc_needed() = %d\n\n", tilemap_recalc_needed());
    }
    else
         printf("Tilemap: Recalc -> Not Needed: tilemap_recalc_needed() = %d\n\n", tilemap_recalc_needed());

    dialog_ui_update();

}


static void dialog_ui_update(void) {

    // If Tilemap calculation succeeded, enable copy-to-clipboard button, otherwise disable
    gtk_widget_set_sensitive(action_maptoclipboard_button, (tilemap_recalc_needed() == FALSE));

    info_display_update();
}


static void info_display_update(void) {

    // TODO: Split out to new file, return strings

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    gint final_bitsperpixel;
    gint tilemap_storage_size;

    // TODO: FIXME: implement better handling for valid map data (tilemap_is_valid()?)
    // TODO: maybe display "no valid data" when no valid tile map calculated (or maybe not, since it's less startling when comparing tile sizes)

    if (tilemap_recalc_needed() == FALSE) {

        p_map      = tilemap_get_map();
        p_tile_set = tilemap_get_tile_set();

        // Use bpp from source image when finalbpp combo is "0", i.e "Src Image"
        // Otherwise use combo value directly
        if (dialog_settings.finalbpp == 0) {
            // Convert source image bytes into bits-per-pixel
            final_bitsperpixel = p_tile_set->tile_bytes_per_pixel * 8;
        }
        else
            final_bitsperpixel = dialog_settings.finalbpp;


        // Use u8 for tilemap array when possible, otherwise u16
        if (p_tile_set->tile_count > 255) // || (p_map->width_in_tiles > 255) || (p_map->height_in_tiles > 255))
            tilemap_storage_size = sizeof(uint16_t);
        else
            tilemap_storage_size = sizeof(uint8_t);

        gtk_label_set_markup(GTK_LABEL(tile_info_display),
             g_markup_printf_escaped(
                "<b>Tile Info</b>\n"
                "<span font_family='monospace'>"
                    "Size:     %4d x %-4d\n"
                    "Tiled Map:%4d x %-4d\n"
                    "Image:    %4d x %-4d\n"
                    "\n"
                    "Map # Tiles:   %4d\n"
                    "Unique # Tiles:%4d\n"
                "</span>"
                 ,
                 p_map->tile_width,     p_map->tile_height,
                 p_map->width_in_tiles, p_map->height_in_tiles,
                 p_map->map_width,      p_map->map_height,
                 (p_map->width_in_tiles * p_map->height_in_tiles),
                 p_tile_set->tile_count));

        gtk_label_set_markup(GTK_LABEL(memory_info_display),
             g_markup_printf_escaped(
                "<b>Memory Info (in bytes)</b>\n"
                "<span font_family='monospace'>"
                   "Tile: %'6d\n"
                   "Tile Set: %'6d\n"
                   "Map Entry: %'6d\n"
                   "Map Total: %'6d\n"
                   "Map + Tiles: %'6d"
                "</span>"
                ,
                // Tile Bytes
                ((p_map->tile_width * p_map->tile_height) * final_bitsperpixel) / 8,  // / 8 bits per byte
                // Tile Set Bytes
                ((p_map->tile_width * p_map->tile_height) * final_bitsperpixel * p_tile_set->tile_count) / 8,  // / 8 bits per byte
                // Tile Map Var Size & Tile Map Bytes
                tilemap_storage_size,
                (p_map->width_in_tiles * p_map->height_in_tiles) * tilemap_storage_size,
                // Total Bytes
                (((p_map->tile_width * p_map->tile_height) * final_bitsperpixel * p_tile_set->tile_count) / 8)  // / 8 bits per byte
                 + ((p_map->width_in_tiles * p_map->height_in_tiles) * tilemap_storage_size)
                 ));
    } // end: if (tilemap_recalc_needed() == FALSE) {
    else {
        gtk_label_set_markup(GTK_LABEL(tile_info_display),
            g_markup_printf_escaped("<b>Tile Info</b>\n\n** No tiles available **\n ► Check tile sizing ◄" ));

        // Padding at the end of the printout to keep widget text height constant
        gtk_label_set_markup(GTK_LABEL(memory_info_display),
            g_markup_printf_escaped("<b>Memory Info (in bytes)</b>\n"
                                    "<span font_family='monospace'>\n\n\n\n\n\n</span>"));

    }

}


#define TILEMAP_MAX_STR 1000000

static void tilemap_copy_map_to_clipboard(void) {

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    GtkClipboard *clipboard;

    char            * map_text_str;
    uint32_t        map_text_len;


    if (tilemap_recalc_needed() == FALSE) {

        map_text_str = malloc(TILEMAP_MAX_STR);

        if (map_text_str) {

            p_map      = tilemap_get_map();
            p_tile_set = tilemap_get_tile_set();

            switch (dialog_settings.maptoclipboard_type) {
                case EXPORT_COPY_TYPE_C:
                    map_text_len = tilemap_export_c_source_to_string(map_text_str,
                                                                     TILEMAP_MAX_STR,
                                                                     dialog_settings.maptoclipboard_prefix_str,
                                                                     p_map,
                                                                     p_tile_set);
                    break;

                case EXPORT_COPY_TYPE_ASM_RGBDS:
                    map_text_len = tilemap_export_asm_rgbds_source_to_string(map_text_str,
                                                                     TILEMAP_MAX_STR,
                                                                     dialog_settings.maptoclipboard_prefix_str,
                                                                     p_map,
                                                                     p_tile_set);
                    break;
            }


            if (map_text_len) {
                // Get a handle to the given clipboard.
                clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                gtk_clipboard_set_text(clipboard, map_text_str, map_text_len);
            }

            free(map_text_str);
        }
    }
}



static void tilemap_render_overlay(void) {

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    p_map      = tilemap_get_map();
    p_tile_set = tilemap_get_tile_set();

    if (p_tile_set->tile_count > 0)
        tilemap_overlay_apply(p_map->size, p_map->tile_id_list);
    else
        printf("Overlay: Render tilenums -> NO TILES FOUND!\n");
}



static void tilemap_preview_highlight_tiles_on_mouseclick(gint x, gint y, GtkAllocation widget_alloc) {

    //
    #define PREVIEW_WIDGET_BORDER_X 1
    #define PREVIEW_WIDGET_BORDER_Y 2


    guint32 tile_id;
    guint32 map_tile_x, map_tile_y, map_tile_idx;
    guint32 img_x, img_y;

    scaled_output_info * scaled_output;

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    // Only display if there's valid data available (no recalc queued)
    if (!(scaled_output_check_reapply_scale() || tilemap_recalc_needed() )) {

            p_map      = tilemap_get_map();
            p_tile_set = tilemap_get_tile_set();

            scaled_output = scaled_info_get();

            // * Mouse location is in preview window coordinates
            // * Scaled preview image may be smaller and centered in preview window
            // So: position on image = mouse.x - (alloc.width - scaled_output->width) / 2,

            img_x = x - ((widget_alloc.width - scaled_output->width) / 2) - PREVIEW_WIDGET_BORDER_X;
            img_y = y - ((widget_alloc.height - scaled_output->height) / 2) - PREVIEW_WIDGET_BORDER_Y;

        // Only process if it's within the bounds of the actual preview area
        if ((img_x >= 0) && (img_x < scaled_output->width) &&
            (img_y >= 0) && (img_y < scaled_output->height)) {

            if (p_tile_set->tile_count > 0) {

                // Get position on tile map and relevant info for tile
                map_tile_x = (img_x / scaled_output->scale_factor) / p_map->tile_width;
                map_tile_y = (img_y / scaled_output->scale_factor) / p_map->tile_height;

                map_tile_idx = map_tile_x + (map_tile_y * p_map->width_in_tiles );

                tile_id = p_map->tile_id_list[map_tile_idx];

                tilemap_overlay_set_highlight_tile(tile_id);

                overlay_redraw_invalidate();
            }

        } else {
            // Click outside of image area, clear highlight
            tilemap_overlay_clear_highlight_tile();
            overlay_redraw_invalidate();
        }
    }

}



static void tilemap_preview_display_tilenum_on_mouseover(gint x, gint y, GtkAllocation widget_alloc) {

    //
    #define PREVIEW_WIDGET_BORDER_X 1
    #define PREVIEW_WIDGET_BORDER_Y 2


    guint32 tile_id;
    guint32 map_tile_x, map_tile_y, map_tile_idx;
    guint32 img_x, img_y;
    uint8_t r,g,b;

    scaled_output_info * scaled_output;

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    // Only display if there's valid data available (no recalc queued)
    if (!(scaled_output_check_reapply_scale() || tilemap_recalc_needed() )) {

            p_map      = tilemap_get_map();
            p_tile_set = tilemap_get_tile_set();

            scaled_output = scaled_info_get();

            // * Mouse location is in preview window coordinates
            // * Scaled preview image may be smaller and centered in preview window
            // So: position on image = mouse.x - (alloc.width - scaled_output->width) / 2,

            img_x = x - ((widget_alloc.width - scaled_output->width) / 2) - PREVIEW_WIDGET_BORDER_X;
            img_y = y - ((widget_alloc.height - scaled_output->height) / 2) - PREVIEW_WIDGET_BORDER_Y;

        // Only process if it's within the bounds of the actual preview area
        if ((img_x >= 0) && (img_x < scaled_output->width) &&
            (img_y >= 0) && (img_y < scaled_output->height)) {

            if (p_tile_set->tile_count > 0) {

                // Get position on tile map and relevant info for tile
                map_tile_x = (img_x / scaled_output->scale_factor) / p_map->tile_width;
                map_tile_y = (img_y / scaled_output->scale_factor) / p_map->tile_height;

                map_tile_idx = map_tile_x + (map_tile_y * p_map->width_in_tiles );

                tile_id = p_map->tile_id_list[map_tile_idx];

                scale_output_get_rgb_at_xy(img_x, img_y, &r, &g, &b);

                gtk_label_set_markup(GTK_LABEL(mouse_hover_display),
                            g_markup_printf_escaped(" x,y: (%4d ,%-4d)"
                                                    "     Map Tile x,y: (%4d , %-4d)"
                                                    "     Map Tile #: %-8d"
                                                    "    Tile ID: %d %s (%d uses)"
                                                    "       RGB(%d,%d,%d)"
                                                    , img_x / scaled_output->scale_factor
                                                    , img_y / scaled_output->scale_factor
                                                    , map_tile_x, map_tile_y
                                                    , map_tile_idx
                                                    , tile_id
                                                    , tile_flip_str[p_map->tile_attribs_list[map_tile_idx]]
                                                    , p_tile_set->tiles[tile_id].map_entry_count
                                                    , r, g, b
                                                    ) );
            }
            else gtk_label_set_markup(GTK_LABEL(mouse_hover_display),
                     g_markup_printf_escaped("  ( No tiles available - check tile sizing )" ) );

        } else gtk_label_set_markup(GTK_LABEL(mouse_hover_display),
                     g_markup_printf_escaped(" " ) );
    }
    else gtk_label_set_markup(GTK_LABEL(mouse_hover_display),
                     g_markup_printf_escaped("  ( No tiles available - check tile sizing )" ) );

}


void dialog_free_resources(void) {

    dialog_source_image_free_and_reset();
}
