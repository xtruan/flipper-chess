#pragma once

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

#define FLIPCHESS_NFC_FEN_MAX_LENGTH 94  // Matches FILE_MAX_CHARS from flipchess_file.h

typedef enum {
    FlipChessNfcStatusSuccess,
    FlipChessNfcStatusErrorNoDevice,
    FlipChessNfcStatusErrorRead,
    FlipChessNfcStatusErrorWrite,
    FlipChessNfcStatusErrorFormat,
    FlipChessNfcStatusErrorInvalidData,
    FlipChessNfcStatusErrorNotNtag,
} FlipChessNfcStatus;

typedef struct FlipChessNfc FlipChessNfc;

/**
 * @brief Allocate FlipChess NFC handler
 * @return FlipChessNfc instance
 */
FlipChessNfc* flipchess_nfc_alloc(void);

/**
 * @brief Free FlipChess NFC handler
 * @param instance FlipChessNfc instance
 */
void flipchess_nfc_free(FlipChessNfc* instance);

/**
 * @brief Read FEN string from NFC card
 * @param instance FlipChessNfc instance
 * @param fen_buffer Buffer to store FEN string (must be at least FLIPCHESS_NFC_FEN_MAX_LENGTH + 1)
 * @return Status code
 */
FlipChessNfcStatus flipchess_nfc_read_board(FlipChessNfc* instance, char* fen_buffer);

/**
 * @brief Write FEN string to NFC card
 * @param instance FlipChessNfc instance
 * @param fen_string FEN string to write
 * @return Status code
 */
FlipChessNfcStatus flipchess_nfc_write_board(FlipChessNfc* instance, const char* fen_string);

/**
 * @brief Check if card is present and is NTAG compatible
 * @param instance FlipChessNfc instance
 * @return true if valid NTAG card detected
 */
bool flipchess_nfc_is_card_present(FlipChessNfc* instance);

/**
 * @brief Get human-readable error message
 * @param status Status code
 * @return Error message string
 */
const char* flipchess_nfc_get_error_message(FlipChessNfcStatus status);