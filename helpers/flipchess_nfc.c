#include "flipchess_nfc.h"
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>
#include <nfc/helpers/nfc_data_generator.h>

#define TAG "FlipChessNFC"

// NTAG213 has 39 pages usable for user data (pages 4-42)
// Each page is 4 bytes, giving us 156 bytes total
// We'll use NDEF format for compatibility
#define FLIPCHESS_NFC_NDEF_HEADER_SIZE 5
#define FLIPCHESS_NFC_TEXT_RECORD_HEADER_SIZE 7
#define FLIPCHESS_NFC_MAX_FEN_STORAGE 94

struct FlipChessNfc {
    Nfc* nfc;
    NfcDevice* nfc_device;
};

FlipChessNfc* flipchess_nfc_alloc(void) {
    FlipChessNfc* instance = malloc(sizeof(FlipChessNfc));
    instance->nfc = nfc_alloc();
    instance->nfc_device = nfc_device_alloc();
    return instance;
}

void flipchess_nfc_free(FlipChessNfc* instance) {
    furi_assert(instance);
    nfc_device_free(instance->nfc_device);
    nfc_free(instance->nfc);
    free(instance);
}

static bool flipchess_nfc_create_ndef_message(const char* fen_string, uint8_t* buffer, size_t* length) {
    size_t fen_len = strlen(fen_string);
    if(fen_len > FLIPCHESS_NFC_MAX_FEN_STORAGE) {
        return false;
    }

    size_t idx = 0;

    // NDEF Message TLV
    buffer[idx++] = 0x03; // NDEF Message TLV tag
    
    // Calculate payload length (will be filled later)
    size_t length_pos = idx;
    idx++; // Reserve space for length byte
    
    // NDEF Record Header
    buffer[idx++] = 0xD1; // MB=1, ME=1, CF=0, SR=1, IL=0, TNF=001 (Well-known)
    
    // Type Length
    buffer[idx++] = 0x01; // Type length = 1 byte
    
    // Payload Length
    buffer[idx++] = fen_len + 3; // FEN + language code length (1) + language code (2)
    
    // Record Type
    buffer[idx++] = 'T'; // Text Record
    
    // Payload - Language Code Status
    buffer[idx++] = 0x02; // UTF-8, language code length = 2
    
    // Payload - Language Code
    buffer[idx++] = 'e';
    buffer[idx++] = 'n';
    
    // Payload - Text (FEN string)
    memcpy(&buffer[idx], fen_string, fen_len);
    idx += fen_len;
    
    // Terminator TLV
    buffer[idx++] = 0xFE;
    
    // Fill in the NDEF message length
    buffer[length_pos] = idx - length_pos - 1;
    
    *length = idx;
    return true;
}

static bool flipchess_nfc_parse_ndef_message(const uint8_t* buffer, size_t buffer_size, char* fen_buffer) {
    // Look for NDEF Message TLV (0x03)
    size_t idx = 0;
    while(idx < buffer_size && buffer[idx] != 0x03) {
        if(buffer[idx] == 0x00) {
            idx++;
            continue;
        }
        if(buffer[idx] == 0xFE) {
            return false; // Terminator before NDEF message
        }
        // Skip unknown TLVs
        idx++;
        if(idx >= buffer_size) return false;
        idx += buffer[idx] + 1;
    }
    
    if(idx >= buffer_size) return false;
    
    idx++; // Skip TLV tag
    if(idx >= buffer_size) return false;
    
    uint8_t ndef_length = buffer[idx++];
    if(idx + ndef_length > buffer_size) return false;
    
    // Parse NDEF Record
    if(idx >= buffer_size) return false;
    uint8_t header = buffer[idx++];
    
    // Check if it's a Text Record (TNF=1, Type='T')
    if((header & 0x07) != 0x01) return false; // TNF must be Well-known
    
    if(idx >= buffer_size) return false;
    uint8_t type_length = buffer[idx++];
    
    if(idx >= buffer_size) return false;
    uint8_t payload_length = buffer[idx++];
    
    if(idx >= buffer_size) return false;
    if(buffer[idx++] != 'T') return false; // Must be Text Record
    
    // Skip language code
    if(idx >= buffer_size) return false;
    uint8_t lang_code_length = buffer[idx++] & 0x3F;
    idx += lang_code_length;
    
    // Calculate actual text length
    size_t text_length = payload_length - lang_code_length - 1;
    if(text_length > FLIPCHESS_NFC_MAX_FEN_STORAGE) {
        text_length = FLIPCHESS_NFC_MAX_FEN_STORAGE;
    }
    
    // Copy FEN string
    if(idx + text_length > buffer_size) return false;
    memcpy(fen_buffer, &buffer[idx], text_length);
    fen_buffer[text_length] = '\0';
    
    return true;
}

FlipChessNfcStatus flipchess_nfc_read_board(FlipChessNfc* instance, char* fen_buffer) {
    furi_assert(instance);
    furi_assert(fen_buffer);
    
    // Start NFC detection
    NfcError error = nfc_poller_detect(instance->nfc, &instance->nfc_device);
    if(error != NfcErrorNone) {
        FURI_LOG_E(TAG, "Failed to detect NFC card");
        return FlipChessNfcStatusErrorNoDevice;
    }
    
    // Check if it's an MF Ultralight card (NTAG)
    const NfcProtocol protocol = nfc_device_get_protocol(instance->nfc_device);
    if(protocol != NfcProtocolMfUltralight) {
        FURI_LOG_E(TAG, "Not an NTAG/MF Ultralight card");
        return FlipChessNfcStatusErrorNotNtag;
    }
    
    // Get card data
    const MfUltralightData* data = nfc_device_get_data(instance->nfc_device, NfcProtocolMfUltralight);
    if(!data) {
        return FlipChessNfcStatusErrorRead;
    }
    
    // Read user data area (starts at page 4)
    // We'll read up to 40 pages (160 bytes) to get the full NDEF message
    uint8_t buffer[160];
    size_t buffer_size = 0;
    
    for(size_t page = 4; page < 44 && page < data->pages_total; page++) {
        memcpy(&buffer[buffer_size], data->page[page].data, 4);
        buffer_size += 4;
        
        // Check for terminator TLV
        if(data->page[page].data[0] == 0xFE || 
           data->page[page].data[1] == 0xFE ||
           data->page[page].data[2] == 0xFE ||
           data->page[page].data[3] == 0xFE) {
            break;
        }
    }
    
    // Parse NDEF message
    if(!flipchess_nfc_parse_ndef_message(buffer, buffer_size, fen_buffer)) {
        FURI_LOG_E(TAG, "Failed to parse NDEF message");
        return FlipChessNfcStatusErrorInvalidData;
    }
    
    FURI_LOG_I(TAG, "Successfully read FEN: %s", fen_buffer);
    return FlipChessNfcStatusSuccess;
}

FlipChessNfcStatus flipchess_nfc_write_board(FlipChessNfc* instance, const char* fen_string) {
    furi_assert(instance);
    furi_assert(fen_string);
    
    // Validate FEN string length
    if(strlen(fen_string) > FLIPCHESS_NFC_MAX_FEN_STORAGE) {
        return FlipChessNfcStatusErrorFormat;
    }
    
    // Create NDEF message
    uint8_t ndef_buffer[200];
    size_t ndef_length = 0;
    if(!flipchess_nfc_create_ndef_message(fen_string, ndef_buffer, &ndef_length)) {
        return FlipChessNfcStatusErrorFormat;
    }
    
    // Start NFC detection
    NfcError error = nfc_poller_detect(instance->nfc, &instance->nfc_device);
    if(error != NfcErrorNone) {
        return FlipChessNfcStatusErrorNoDevice;
    }
    
    // Check if it's an MF Ultralight card
    const NfcProtocol protocol = nfc_device_get_protocol(instance->nfc_device);
    if(protocol != NfcProtocolMfUltralight) {
        return FlipChessNfcStatusErrorNotNtag;
    }
    
    // Prepare write data
    MfUltralightData* data = malloc(sizeof(MfUltralightData));
    memcpy(data, nfc_device_get_data(instance->nfc_device, NfcProtocolMfUltralight), sizeof(MfUltralightData));
    
    // Write NDEF message to pages 4 onwards
    size_t ndef_idx = 0;
    for(size_t page = 4; page < 44 && ndef_idx < ndef_length; page++) {
        for(size_t byte = 0; byte < 4 && ndef_idx < ndef_length; byte++) {
            data->page[page].data[byte] = ndef_buffer[ndef_idx++];
        }
        // Pad remaining bytes with 0x00
        while(ndef_idx % 4 != 0 && ndef_idx < ndef_length + 4) {
            data->page[page].data[ndef_idx % 4] = 0x00;
            ndef_idx++;
        }
    }
    
    // Write to card using poller
    MfUltralightError write_error = MfUltralightErrorNone;
    nfc_device_set_data(instance->nfc_device, NfcProtocolMfUltralight, data);
    
    // Attempt to write using the sync API
    MfUltralightPollerSync* poller_sync = mf_ultralight_poller_sync_alloc(instance->nfc);
    
    for(size_t page = 4; page < 44 && (page - 4) * 4 < ndef_length; page++) {
        write_error = mf_ultralight_poller_sync_write_page(poller_sync, page, data->page[page].data);
        if(write_error != MfUltralightErrorNone) {
            FURI_LOG_E(TAG, "Failed to write page %zu", page);
            break;
        }
    }
    
    mf_ultralight_poller_sync_free(poller_sync);
    free(data);
    
    if(write_error != MfUltralightErrorNone) {
        return FlipChessNfcStatusErrorWrite;
    }
    
    FURI_LOG_I(TAG, "Successfully wrote FEN to NFC");
    return FlipChessNfcStatusSuccess;
}

bool flipchess_nfc_is_card_present(FlipChessNfc* instance) {
    furi_assert(instance);
    
    NfcError error = nfc_poller_detect(instance->nfc, &instance->nfc_device);
    if(error != NfcErrorNone) {
        return false;
    }
    
    const NfcProtocol protocol = nfc_device_get_protocol(instance->nfc_device);
    return protocol == NfcProtocolMfUltralight;
}

const char* flipchess_nfc_get_error_message(FlipChessNfcStatus status) {
    switch(status) {
    case FlipChessNfcStatusSuccess:
        return "Success";
    case FlipChessNfcStatusErrorNoDevice:
        return "No NFC card detected";
    case FlipChessNfcStatusErrorRead:
        return "Failed to read card";
    case FlipChessNfcStatusErrorWrite:
        return "Failed to write card";
    case FlipChessNfcStatusErrorFormat:
        return "Invalid data format";
    case FlipChessNfcStatusErrorInvalidData:
        return "Invalid NDEF data";
    case FlipChessNfcStatusErrorNotNtag:
        return "Not an NTAG card";
    default:
        return "Unknown error";
    }
}