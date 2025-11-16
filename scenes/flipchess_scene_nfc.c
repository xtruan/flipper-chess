#include "../flipchess.h"
#include "../helpers/flipchess_nfc.h"
#include "../helpers/flipchess_haptic.h"
#include <gui/elements.h>

typedef enum {
    FlipChessNfcStateIdle,
    FlipChessNfcStateReading,
    FlipChessNfcStateWriting,
    FlipChessNfcStateSuccess,
    FlipChessNfcStateError,
} FlipChessNfcState;

typedef struct {
    FlipChessNfcState state;
    FlipChessNfcStatus last_status;
    bool is_write_mode;
    char status_message[64];
} FlipChessSceneNfcModel;

static void flipchess_scene_nfc_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    FlipChessSceneNfcModel* model = context;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    // Title
    if(model->is_write_mode) {
        canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop, "Save to NFC");
    } else {
        canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop, "Load from NFC");
    }
    
    // Icon area - draw NFC symbol
    canvas_set_font(canvas, FontSecondary);
    // canvas_draw_icon(canvas, 46, 20, &I_NFC_manual_60x50); // Use built-in NFC icon
    
    // Status message
    canvas_set_font(canvas, FontSecondary);
    
    switch(model->state) {
    case FlipChessNfcStateIdle:
        if(model->is_write_mode) {
            canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "Hold card to");
            canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, "back of Flipper");
        } else {
            canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "Hold card to");
            canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, "back of Flipper");
        }
        break;
        
    case FlipChessNfcStateReading:
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "Reading...");
        break;
        
    case FlipChessNfcStateWriting:
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "Writing...");
        break;
        
    case FlipChessNfcStateSuccess:
        // canvas_draw_icon(canvas, 54, 35, &I_DolphinCommon_56x48);
        if(model->is_write_mode) {
            canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "Saved!");
        } else {
            canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "Loaded!");
        }
        break;
        
    case FlipChessNfcStateError:
        canvas_draw_icon(canvas, 54, 35, &I_DolphinCommon_56x48);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "Error:");
        canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, model->status_message);
        break;
    }
    
    // Instructions
    canvas_set_font(canvas, FontSecondary);
    if(model->state == FlipChessNfcStateIdle) {
        elements_button_center(canvas, "Scan");
    } else if(model->state == FlipChessNfcStateSuccess || model->state == FlipChessNfcStateError) {
        elements_button_center(canvas, "OK");
    }
}

static void flipchess_scene_nfc_perform_operation(FlipChess* app, FlipChessSceneNfcModel* model) {
    FlipChessNfc* nfc = flipchess_nfc_alloc();
    FlipChessNfcStatus status;
    
    if(model->is_write_mode) {
        // Write current game state to NFC
        model->state = FlipChessNfcStateWriting;
        
        char fen_buffer[FLIPCHESS_NFC_FEN_MAX_LENGTH + 1] = {0};
        
        // Get current board state from saved game or generate new one
        if(app->import_game == 1 && strlen(app->import_game_text) > 0) {
            strncpy(fen_buffer, app->import_game_text, FLIPCHESS_NFC_FEN_MAX_LENGTH);
        } else {
            // No game to save
            model->state = FlipChessNfcStateError;
            strncpy(model->status_message, "No game to save", sizeof(model->status_message));
            flipchess_nfc_free(nfc);
            return;
        }
        
        status = flipchess_nfc_write_board(nfc, fen_buffer);
        
    } else {
        // Read from NFC
        model->state = FlipChessNfcStateReading;
        
        char fen_buffer[FLIPCHESS_NFC_FEN_MAX_LENGTH + 1] = {0};
        status = flipchess_nfc_read_board(nfc, fen_buffer);
        
        if(status == FlipChessNfcStatusSuccess) {
            // Store the loaded FEN in the app
            strncpy(app->import_game_text, fen_buffer, sizeof(app->import_game_text) - 1);
            app->import_game_text[sizeof(app->import_game_text) - 1] = '\0';
            app->import_game = 1;
        }
    }
    
    flipchess_nfc_free(nfc);
    
    // Update model based on result
    model->last_status = status;
    if(status == FlipChessNfcStatusSuccess) {
        model->state = FlipChessNfcStateSuccess;
        flipchess_play_happy_bump(app);
    } else {
        model->state = FlipChessNfcStateError;
        const char* error_msg = flipchess_nfc_get_error_message(status);
        strncpy(model->status_message, error_msg, sizeof(model->status_message) - 1);
        model->status_message[sizeof(model->status_message) - 1] = '\0';
        flipchess_play_long_bump(app);
    }
}

static bool flipchess_scene_nfc_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    FlipChess* app = context;
    bool consumed = false;
    
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyBack) {
            // Go back to menu
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        } else if(event->key == InputKeyOk) {
            with_view_model(
                app->nfc_view,
                FlipChessSceneNfcModel* model,
                {
                    if(model->state == FlipChessNfcStateIdle) {
                        // Start NFC operation
                        flipchess_scene_nfc_perform_operation(app, model);
                    } else if(model->state == FlipChessNfcStateSuccess) {
                        // Success - go back or to game
                        if(!model->is_write_mode) {
                            // After successful load, go to game
                            scene_manager_next_scene(app->scene_manager, FlipChessSceneScene_1);
                        } else {
                            // After successful save, go back
                            scene_manager_previous_scene(app->scene_manager);
                        }
                    } else if(model->state == FlipChessNfcStateError) {
                        // Error - reset to idle
                        model->state = FlipChessNfcStateIdle;
                    }
                },
                true);
            consumed = true;
        }
    }
    
    return consumed;
}

void flipchess_scene_nfc_on_enter(void* context) {
    furi_assert(context);
    FlipChess* app = context;
    
    // Get the operation mode from scene state
    uint32_t scene_state = scene_manager_get_scene_state(app->scene_manager, FlipChessSceneNfc);
    bool is_write = (scene_state == 1);
    
    with_view_model(
        app->nfc_view,
        FlipChessSceneNfcModel* model,
        {
            model->state = FlipChessNfcStateIdle;
            model->is_write_mode = is_write;
            model->last_status = FlipChessNfcStatusSuccess;
            model->status_message[0] = '\0';
        },
        true);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipChessViewIdNfc);
}

bool flipchess_scene_nfc_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipchess_scene_nfc_on_exit(void* context) {
    UNUSED(context);
    // Cleanup if needed
}

View* flipchess_scene_nfc_alloc(void) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(FlipChessSceneNfcModel));
    view_set_draw_callback(view, flipchess_scene_nfc_draw_callback);
    view_set_input_callback(view, flipchess_scene_nfc_input_callback);
    return view;
}

void flipchess_scene_nfc_free(View* view) {
    furi_assert(view);
    view_free(view);
}