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

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "filter_tilemap_helper.h"
#include "filter_dialog.h"
#include "scale.h"
#include "lib_tilemap.h"


extern const char PLUG_IN_PROCEDURE[];
extern const char PLUG_IN_ROLE[];
extern const char PLUG_IN_BINARY[];

static void dialog_scaled_preview_check_resize(GtkWidget *, gint, gint, gint);
static void resize_image_and_apply_changes(GimpDrawable *, guchar *, guint);
// static void on_setting_scaler_combo_changed (GtkComboBox *, gpointer);

static void on_setting_scale_spinbutton_changed(GtkSpinButton *, gpointer);
static void on_setting_tilesize_spinbutton_changed(GtkSpinButton *, gint);

static void dialog_settings_apply_to_ui();
static void dialog_settings_connect_signals(GimpDrawable *);

static void update_text_readout();

gboolean preview_scaled_update(GtkWidget *, GdkEvent *, GtkWidget *);

static void tilemap_calculate(uint8_t *, gint, gint, gint);
static void tilemap_invalidate();
static void tilemap_printinfo(gint, gint, gint);

// Widget for displaying the upscaled image preview
static GtkWidget * preview_scaled;
static GtkWidget * info_display;


// TODO: consider passing parent vbox into widget creation so these can be moved to local vars (?)
static GtkWidget * setting_scale_label;
static GtkWidget * setting_scale_spinbutton;

static GtkWidget * setting_overlay_checkbutton;

static GtkWidget * setting_tilesize_label;
static GtkWidget * setting_tilesize_width_spinbutton;
static GtkWidget * setting_tilesize_height_spinbutton;

static GtkWidget * setting_checkmirror_checkbutton;
static GtkWidget * setting_checkrotation_checkbutton;


static PluginTileMapVals dialog_settings;


// TODO: move these out of global scope?
static image_data      app_image;
static color_data      app_colors;
static gint            tilemap_needs_recalc;


/*
Preview
x Zoom [1]^
x [x] Overlay

Processing:
[x] Deduplicate Tiles
    [ ] Check Rotation
    [ ] Check Mirroring

Tiles:
Width[ 8 ]
Height[ 8 ]
Size from Image Grid [ ]

Offset:
X[ 0 ]
Y[ 0 ]

Info:
Tile W x H
Image W x H
Tile Map W x H
Total Colors N / Max colors per tile N
Color Mode: Indexed / Etc
*/


/*******************************************************/
/*               Main Plug-in Dialog                   */
/*******************************************************/
gboolean tilemap_dialog_show (GimpDrawable *drawable)
{
    GtkWidget * dialog;
    GtkWidget * main_vbox;
    GtkWidget * preview_hbox;

    GtkWidget * scaled_preview_window;

    GtkWidget * setting_table;
    GtkWidget * setting_preview_label;
    GtkWidget * setting_processing_label;

    // Info
//    GtkWidget * info_display;


    gboolean   run;
    gint       idx;


    gimp_ui_init (PLUG_IN_BINARY, FALSE);

    dialog = gimp_dialog_new ("Tilemap Helper", PLUG_IN_ROLE,
                            NULL, 0,
                            gimp_standard_help_func, PLUG_IN_PROCEDURE,

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

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

    // Create 2 x 3 table for Settings, non-homogonous sizing, attach to main vbox
    // TODO: Consider changing from a table to a grid (tables are deprecated)
    setting_table = gtk_table_new (5, 5, FALSE);
    gtk_box_pack_start (GTK_BOX (main_vbox), setting_table, FALSE, FALSE, 0);
    gtk_table_set_row_spacings(GTK_TABLE(setting_table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(setting_table), 20);
    //gtk_table_set_homogeneous(GTK_TABLE (setting_table), TRUE);
    gtk_table_set_homogeneous(GTK_TABLE (setting_table), FALSE);


    // == Preview Settings ==
    setting_preview_label = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(setting_preview_label), "<b>Preview</b>");
    gtk_misc_set_alignment(GTK_MISC(setting_preview_label), 0.0f, 0.5f); // Left-align

        // Checkbox for the image overlay
        setting_overlay_checkbutton = gtk_check_button_new_with_label("Overlay");
        gtk_misc_set_alignment(GTK_MISC(setting_overlay_checkbutton), 1.0f, 0.5f); // Right-align

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
        setting_tilesize_width_spinbutton  = gtk_spin_button_new_with_range(1,256,1); // Min/Max/Step
        setting_tilesize_height_spinbutton = gtk_spin_button_new_with_range(1,256,1); // Min/Max/Step

        // Checkboxes for mirroring and rotation on tile deduplication
        setting_checkmirror_checkbutton = gtk_check_button_new_with_label("Check Mirroring");
        setting_checkrotation_checkbutton = gtk_check_button_new_with_label("Check Rotation");

    // Info readout/display area
        // TODO: move to span entire bottom row, or use workarounds for variable width
    info_display = gtk_label_new (NULL);
    gtk_label_set_markup(GTK_LABEL(info_display),
                         g_markup_printf_escaped("Tile: %d x %d\n"
                                                 "Image: %d x %d\n"
                                                 "Tiled Map: %d x %d\n"
                                                 "Total Colors: %d\n"
                                                 "Max colors per tile: %d (#%d)\n"
                                                 "Color Mode: Indexed", 8,8, 640,480, 80, 60, 16, 8, 12));
    gtk_label_set_max_width_chars(GTK_LABEL(info_display), 29);
    gtk_label_set_ellipsize(GTK_LABEL(info_display),PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(info_display), 0.0f, 0.5f); // Left-align



    // Attach the UI WIdgets to the table and show them all
    // gtk_table_attach_defaults (*attach_to, *widget, left_attach, right_attach, top_attach, bottom_attach)
    //
    gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_preview_label,            0, 2, 0, 1);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_scale_label,          0, 2, 1, 2);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_scale_spinbutton,     0, 1, 2, 3);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_overlay_checkbutton,  0, 2, 3, 4);

    gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_processing_label,                2, 4, 0, 1);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_tilesize_label,              2, 4, 1, 2);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_tilesize_width_spinbutton,   2, 3, 2, 3);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_tilesize_height_spinbutton,  3, 4, 2, 3);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_checkmirror_checkbutton,     2, 4, 3, 4);
        gtk_table_attach_defaults (GTK_TABLE (setting_table), setting_checkrotation_checkbutton,   2, 4, 4, 5);

    gtk_table_attach_defaults (GTK_TABLE (setting_table), info_display,        4, 5, 0, 5); // Middle of table


    gtk_widget_show (setting_table);

    gtk_widget_show (setting_preview_label);
        gtk_widget_show (setting_overlay_checkbutton);
        gtk_widget_show (setting_scale_label);
        gtk_widget_show (setting_scale_spinbutton);

    gtk_widget_show (setting_processing_label);
        gtk_widget_show (setting_tilesize_label);
        gtk_widget_show (setting_tilesize_width_spinbutton);
        gtk_widget_show (setting_tilesize_height_spinbutton);
        gtk_widget_show (setting_overlay_checkbutton);
        gtk_widget_show (setting_checkmirror_checkbutton);
        gtk_widget_show (setting_checkrotation_checkbutton);

    gtk_widget_show (info_display);

    dialog_settings_apply_to_ui();

    dialog_settings_connect_signals(drawable);


    // ======== SHOW THE DIALOG AND RUN IT ========

    tilemap_invalidate();

    gtk_widget_show (dialog);

    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);

    return run;
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



// Load dialog settings into UI (called on startup)
//
void dialog_settings_apply_to_ui() {

    // ======== UPDATE WIDGETS TO CURRENT SETTINGS ========

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_scale_spinbutton),           dialog_settings.scale_factor);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_tilesize_width_spinbutton),  dialog_settings.tile_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(setting_tilesize_height_spinbutton), dialog_settings.tile_height);
}



void dialog_settings_connect_signals(GimpDrawable *drawable) {
    // ======== HANDLE UI CONTROL VALUE UPDATES ========

    // Connect the changed signal to update the scaler mode
    g_signal_connect (setting_scale_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_scale_spinbutton_changed), NULL);

    // Connect the changed signal to update the scaler mode
    g_signal_connect (setting_tilesize_width_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_tilesize_spinbutton_changed), GINT_TO_POINTER(WIDGET_TILESIZE_WIDTH));
    g_signal_connect (setting_tilesize_height_spinbutton, "value-changed",
                      G_CALLBACK (on_setting_tilesize_spinbutton_changed), GINT_TO_POINTER(WIDGET_TILESIZE_HEIGHT));


    // ======== HANDLE PROCESSING UPDATES VIA UI CONTROL VALUE CHANGES ========

    // Connect a second signal to trigger a preview update
    // TODO: just run display processing
    g_signal_connect_swapped (setting_scale_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);

    // TODO: wire this to a separate processing function to run both tile and display processing
    g_signal_connect_swapped (setting_tilesize_width_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);
    g_signal_connect_swapped (setting_tilesize_height_spinbutton, "value-changed",
                              G_CALLBACK(tilemap_dialog_processing_run), drawable);
}


// Handler for "changed" signal of SCALER MODE combo box
// When the user changes the scaler type -> Update the scaler mode
//
//   callback_data not used currently
//
static void on_setting_scale_spinbutton_changed(GtkSpinButton *spinbutton, gpointer callback_data)
{
    dialog_settings.scale_factor = gtk_spin_button_get_value_as_int(spinbutton);
}


// TODO: ?consolidate to a single spin button UI update handler?
static void on_setting_tilesize_spinbutton_changed(GtkSpinButton *spinbutton, gint callback_data)
{
    switch (callback_data) {
        case WIDGET_TILESIZE_WIDTH:  dialog_settings.tile_width  = gtk_spin_button_get_value_as_int(spinbutton);
            tilemap_invalidate();
            break;
        case WIDGET_TILESIZE_HEIGHT: dialog_settings.tile_height = gtk_spin_button_get_value_as_int(spinbutton);
            tilemap_invalidate();
            break;
    }
}


static void update_text_readout()
{
    /*
    gtk_label_set_markup(GTK_LABEL(info_display),
                     g_markup_printf_escaped("Tile: %d x %d\n"
                                             "Image: %d x %d\n"
                                             "Tiled Map: %d x %d\n"
                                             "Total Colors: %d\n"
                                             "Max colors per tile: %d (#%d)\n"
                                             "Color Mode: Indexed",

                                             dialog_settings.tile_width,dialog_settings.tile_height,
                                             640,480,
                                             80, 60,
                                             16, 8,
                                             12));
*/
}


// Checks to see whether the scaled preview area needs
// to be resized. Handles resizing if needed.
//
// Called from pixel_art_scalers_run() which is used for
// previewing and final rendering of the selected scaler mode
//
static void dialog_scaled_preview_check_resize(GtkWidget * preview_scaled, gint width_new, gint height_new, gint scale_factor_new)
{
    gint width_current, height_current;

    // Get current size for scaled preview area
    gtk_widget_get_size_request (preview_scaled, &width_current, &height_current);

    // Only resize if the width, height or scaling changed
    if ( (width_current  != (width_new  * scale_factor_new)) ||
         (height_current != (height_new * scale_factor_new)) )
    {
        // TODO: This queues a second redraw event... it seems to work fine. Does it need to be fixed?
        printf("Check size... Resize applied\n");

        // Resize scaled preview area
        gtk_widget_set_size_request (preview_scaled, width_new * scale_factor_new, height_new * scale_factor_new);

        // when set_size_request and then draw are called repeatedly on a preview_area
        // it causes redraw glitching in the surrounding scrolled_window region
        // Calling set_max_size appears to fix this
        // (though it may be treating the symptom and not the cause of the glitching)
        gimp_preview_area_set_max_size(GIMP_PREVIEW_AREA (preview_scaled),
                                       width_new * scale_factor_new,
                                       height_new * scale_factor_new);
    }
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
    GimpPixelRgn src_rgn;
    gint         bpp;
    gint         width, height;
    gint         x, y;
    uint8_t    * p_srcbuf = NULL;
    uint8_t    * p_tilebuf = NULL; // TODO: prob needs to be pointer pointer, or method to tilebuf_get()
    glong        srcbuf_size = 0;
    scaled_output_info * scaled_output;


printf("tilemap_dialog_processing_run 1 --> tilemap_needs_recalc = %d\n", tilemap_needs_recalc);

    // Apply dialog settings
    scale_factor_set( dialog_settings.scale_factor );
    printf("Redraw queued at %dx\n", dialog_settings.scale_factor);

    // Check for previously rendered output
    scaled_output = scaled_info_get();

    // TODO: Always use the entire image?

    // Get the working image area for either the preview sub-window or the entire image
    if (preview) {
        // gimp_preview_get_position (preview, &x, &y);
        // gimp_preview_get_size (preview, &width, &height);
        if (! gimp_drawable_mask_intersect (drawable->drawable_id,
                                                 &x, &y, &width, &height)) {
            return;
        }

        dialog_scaled_preview_check_resize( preview_scaled, width, height, dialog_settings.scale_factor);
    } else if (! gimp_drawable_mask_intersect (drawable->drawable_id,
                                             &x, &y, &width, &height)) {
        // TODO: DO STUFF WHEN CALLED AFTER DIALOG CLOSED
        return;
    }


    // Get bit depth and alpha mask status
    bpp = drawable->bpp;

    // Allocate output buffer for upscaled image
    scaled_output_check_reallocate(bpp, width, height);


    // TODO: switch this to an invalidate model like tilemap_needs_recalc? - then above could be merged in and nested
    if ((scaled_output_check_reapply_scale()) || (tilemap_needs_recalc)) {

        // ====== GET THE SOURCE IMAGE ======
        // Allocate a working buffer to copy the source image into
        srcbuf_size = width * height * bpp;
        p_srcbuf = (uint8_t *) g_new (guint8, srcbuf_size);


        // FALSE, FALSE : region will be used to read the actual drawable data
        // Initialize source pixel region with drawable
        gimp_pixel_rgn_init (&src_rgn,
                             drawable,
                             x, y,
                             width, height,
                             FALSE, FALSE);

        // Copy source image to working buffer
        gimp_pixel_rgn_get_rect (&src_rgn,
                                 (guchar *) p_srcbuf,
                                 x, y, width, height);

        // ====== CALCULATE TILE MAP & TILES ======

printf("tilemap_dialog_processing_run 2 --> tilemap_needs_recalc = %d\n", tilemap_needs_recalc);
        if (tilemap_needs_recalc) {
            tilemap_calculate(p_srcbuf,
                              bpp,
                              width, height);
        }


        // ====== APPLY THE SCALER ======

        // TODO: remove comment FIX INDEXED HANDLING FIRST ---Expects 4BPP RGBA in p_srcbuf, outputs same to p_scaledbuf
        if (scaled_output_check_reapply_scale()) {
            scale_apply(p_srcbuf,
                        scaled_output->p_scaledbuf,
                        bpp,
                        width, height);
        }
    }
    else

    // Filter is done, apply the update
    if (preview) {


        // Redraw the scaled preview if it's available (it ought to be at this point)
        if ( (scaled_output->p_scaledbuf != NULL) &&
             (scaled_output->valid_image == TRUE) ) {

            // TODO: ? use gimp_preview_area_blend() to mix overlay and source image?
            // Calling widget should be: preview_scaled
            gimp_preview_area_draw (GIMP_PREVIEW_AREA (preview_scaled),
                                    0, 0,                  // x,y
                                    scaled_output->width,  // width, height
                                    scaled_output->height,
                                    gimp_drawable_type (drawable->drawable_id),             // GimpImageType (source image)
                                    (guchar *) scaled_output->p_scaledbuf,      // Source buffer
                                    scaled_output->width * scaled_output->bpp); // Row-stride
        }

        // Update the info display area
        update_text_readout();

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


    // Free the working buffer
    if (p_srcbuf)
        g_free (p_srcbuf);
}




static void tilemap_invalidate() {
    tilemap_needs_recalc = TRUE;
}



static void tilemap_printinfo(gint bpp, gint width, gint height) {

    tile_map_data * p_map;
    tile_set_data * p_tile_set;

    // TODO: check cached tile size a better way than this:
    p_map      = tilemap_get_map();

    printf("tilemap_needs_recalc = %d"
    "\n------\n"
    "tilemap calc: \n"
    "app_image.bytes_per_pixel != bpp (%d , %d) \n"
    "app_image.width      != width (%d , %d) \n"
    "app_image.height     != height (%d , %d) \n"
    "app_image.size       != width * height * bpp (%d , %d) \n"
    "p_map->tile_width    != dialog_settings.tile_width (%d , %d) \n"
    "p_map->tile_height   != dialog_settings.tile_height (%d , %d)\n",
    tilemap_needs_recalc,
    app_image.bytes_per_pixel , bpp,
    app_image.width      , width,
    app_image.height     , height,
    app_image.size       , (width * height * bpp),
    p_map->tile_width    , dialog_settings.tile_width,
    p_map->tile_height   , dialog_settings.tile_height);

}


// TODO: variable tile size (push down via app settings?)
//  gint image_id, gint drawable_id, gint image_mode)
void tilemap_calculate(uint8_t * p_srcbuf, gint bpp, gint width, gint height) {

    gint status;

    tile_map_data * p_map;
    tile_set_data * p_tile_set;
    image_data      tile_set_deduped_image;


    status = TRUE; // Default to success

    tilemap_printinfo(bpp, width, height);

/*
// TODO: FIXME Update getting triggered incorrectly by changes in scale
    // TODO: move into function
    // TODO: or tile size, mirror or rotate changed
    // Did any map related settings change? Then queue an update
    if ((app_image.bytes_per_pixel != bpp)             ||
        (app_image.width      != width)                ||
        (app_image.height     != height)               ||
        (app_image.size       != width * height * bpp) ||
        (p_map->tile_width    != dialog_settings.tile_width) ||
        (p_map->tile_height   != dialog_settings.tile_height))
    {
        tilemap_needs_recalc = TRUE;
        printf("Tilemap Recalc check = True\n");
    }
*/
    // Get the Bytes Per Pixel of the incoming app image
    app_image.bytes_per_pixel = bpp;

    // Determine the array size for the app's image then allocate it
    app_image.width      = width;
    app_image.height     = height;
    app_image.size       = width * height * bpp;
    app_image.p_img_data = p_srcbuf;


    if (tilemap_needs_recalc) {
        status = tilemap_export_process(&app_image,
                                        dialog_settings.tile_width,
                                        dialog_settings.tile_height);

        // TODO: warn/notify on failure (invalid tile size, etc)

        if (status) {

            printf("Tilemap Recalc SUCCESS...\n");

            // Retrieve the deduplicated map and tile set
            p_map      = tilemap_get_map();
            p_tile_set = tilemap_get_tile_set();
            // status     = tilemap_get_image_of_deduped_tile_set(&tile_set_deduped_image);

            // Set tile map parameters, then convert the image to a map
            /*
            p_map->width_in_tiles;
            p_map->height_in_tiles;
            p_tile_set->tile_count;

            p_map->tile_id_list -> uint8_t * p_map_data
            p_map->size



            */

            gtk_label_set_markup(GTK_LABEL(info_display),
                 g_markup_printf_escaped("Tiles: %d x %d\n"
                                         "Image: %d x %d\n"
                                         "Tiled Map: %d x %d\n"
                                         "Map # Tiles: %d\n"
                                         "Unique # Tiles: %d\n"
//                                         "Total Colors: %d\n"
//                                         "Max colors per tile: %d (#%d)\n"
                                         "Color Mode: %d",

                                         p_map->tile_width,     p_map->tile_height,
                                         p_map->map_width,      p_map->map_height,
                                         p_map->width_in_tiles, p_map->height_in_tiles,
                                         (p_map->width_in_tiles * p_map->height_in_tiles),
                                         p_map->size,
//                                         16, 8,
                                         bpp));

            tilemap_needs_recalc = FALSE;
            printf("tilemap:done --> tilemap_needs_recalc = %d\n\n", tilemap_needs_recalc);
        }
    }
}

// TODO
// gint tilemap_check_needs_recalcualte()


// resize_image_and_apply_changes
//
// Resizes image and then draws the newly scaled output onto it.
// This is only for FINAL, NON-PREVIEW rendered output
//
// Called from pixel_art_scalers_run()
//
// Params:
// * GimpDrawable          : from source image
// * guchar * buffer       : the previously rendered scaled output
// * guint    scale_factor : image scale multiplier
//
/*
void resize_image_and_apply_changes(GimpDrawable * drawable, guchar * p_scaledbuf, guint scale_factor)
{
    GimpPixelRgn  dest_rgn;
    gint          x,y, width, height;
    GimpDrawable  * resized_drawable;

    if (! gimp_drawable_mask_intersect (drawable->drawable_id,
                                         &x, &y, &width, &height))
        return;

    // == START UNDO GROUPING
    gimp_image_undo_group_start(gimp_item_get_image(drawable->drawable_id));

    // Resize source image
    if (gimp_image_resize(gimp_item_get_image(drawable->drawable_id),
                          width * scale_factor,
                          height * scale_factor,
                          0,0))
    {

        // Resize the current layer to match the resized image
        gimp_layer_resize_to_image_size( gimp_image_get_active_layer(
                                           gimp_item_get_image(drawable->drawable_id) ) );


        // Get a new drawable handle from the resized layer/image
        resized_drawable = gimp_drawable_get( gimp_image_get_active_drawable(
                                                gimp_item_get_image(drawable->drawable_id) ) );

        // Initialize destination pixel region with drawable
        // TRUE,  TRUE  : region will be used to write to the shadow tiles
        //                i.e. make changes that will be written back to source tiles
        gimp_pixel_rgn_init (&dest_rgn,
                             resized_drawable,
                             0, 0,
                             width * scale_factor,
                             height * scale_factor,
                             TRUE, TRUE);

        // Copy the previously rendered scaled output buffer
        // to the shadow image buffer in the drawable
        gimp_pixel_rgn_set_rect (&dest_rgn,
                                 (guchar *) p_scaledbuf,
                                 0,0,
                                 width * scale_factor,
                                 height * scale_factor);


        // Apply the changes to the image (merge shadow, update drawable)
        gimp_drawable_flush (resized_drawable);
        gimp_drawable_merge_shadow (resized_drawable->drawable_id, TRUE);
        gimp_drawable_update (resized_drawable->drawable_id, 0, 0, width * scale_factor, height * scale_factor);

        // Free the extra resized drawable
        gimp_drawable_detach (resized_drawable);
    }

    // == END GROUPING
    gimp_image_undo_group_end(gimp_item_get_image(drawable->drawable_id));
}
*/
