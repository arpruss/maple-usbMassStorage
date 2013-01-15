
#include "usb_scsi.h"
#include "usb_mass.h"
#include "usb_mass_mal.h"

#include <libmaple/usb.h>
#include <libmaple/nvic.h>
#include <libmaple/delay.h>

/* Private headers */
#include "usb_lib_globals.h"
#include "usb_reg_map.h"

/* usb_lib headers */
#include "usb_type.h"
#include "usb_core.h"
#include "usb_def.h"

#define SCSI_READ_FORMAT_CAPACITY_DATA_LEN   0x0C
#define SCSI_READ_FORMAT_CAPACITY10_DATA_LEN 0x08
#define SCSI_MODE_SENSE6_DATA_LEN            0x04
#define SCSI_MODE_SENSE10_DATA_LEN           0x08

/* See usb_mass.c */
extern BulkOnlyCBW CBW;
extern uint8_t botState;

/* See usb_mass_mal.c */
extern uint32_t MAL_massBlockCount[2];
extern uint32_t MAL_massBlockSize[2];

/* See usb_scsi_data.c */
extern uint8_t SCSI_page00InquiryData[];
extern uint8_t SCSI_standardInquiryData[];
extern uint8_t SCSI_standardInquiryData2[];
extern uint8_t SCSI_senseData[];
extern uint8_t SCSI_modeSense6Data[];
extern uint8_t SCSI_modeSense10Data[];
extern uint8_t SCSI_readFormatCapacityData[];
extern uint8_t SCSI_readFormatCapacity10Data[];

uint32_t SCSI_lba;
uint32_t SCSI_blkLen;

uint8_t scsi_address_management(uint8_t lun, uint8_t cmd, uint32_t lba, uint32_t blockNbr);

void scsi_inquiry_cmd(uint8_t lun) {
  uint8_t* inquiryData;
  uint16_t inquiryDataLength;

  if (CBW.CB[1] & 0x01) /*Evpd is set*/ {
    inquiryData = SCSI_page00InquiryData;
    inquiryDataLength = 5;
  } else {

    if (lun == 0) {
      inquiryData = SCSI_standardInquiryData;
    } else {
      inquiryData = SCSI_standardInquiryData2;
    }

    if (CBW.CB[4] <= SCSI_STANDARD_INQUIRY_DATA_LEN)
      inquiryDataLength = CBW.CB[4];
    else
      inquiryDataLength = SCSI_STANDARD_INQUIRY_DATA_LEN;

  }
  usb_mass_transfer_data_request(inquiryData, inquiryDataLength);
}

void scsi_request_sense_cmd(uint8_t lun) {
  uint8_t requestSenseDataLength;
  if (CBW.CB[4] <= SCSI_REQUEST_SENSE_DATA_LEN) {
    requestSenseDataLength = CBW.CB[4];
  } else {
    requestSenseDataLength = SCSI_REQUEST_SENSE_DATA_LEN;
  }
  usb_mass_transfer_data_request(SCSI_senseData, requestSenseDataLength);
}

void scsi_start_stop_unit_cmd(uint8_t lun) {
  usb_mass_bot_set_csw(BOT_CSW_CMD_PASSED, BOT_SEND_CSW_ENABLE);
}

void scsi_mode_sense6_cmd(uint8_t lun) {
  usb_mass_transfer_data_request(SCSI_modeSense6Data, SCSI_MODE_SENSE6_DATA_LEN);
}

void scsi_mode_sense10_cmd(uint8_t lun) {
  usb_mass_transfer_data_request(SCSI_modeSense10Data, SCSI_MODE_SENSE10_DATA_LEN);
}

void scsi_read_format_capacity_cmd(uint8_t lun) {
  if (usb_mass_mal_get_status(lun) != 0) {
    scsi_set_sense_data(CBW.bLUN, SCSI_NOT_READY, SCSI_MEDIUM_NOT_PRESENT);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_ENABLE);
    usb_mass_bot_abort(BOT_DIR_IN);
    return;
  }
  SCSI_readFormatCapacityData[4] = (uint8_t) (MAL_massBlockCount[lun] >> 24);
  SCSI_readFormatCapacityData[5] = (uint8_t) (MAL_massBlockCount[lun] >> 16);
  SCSI_readFormatCapacityData[6] = (uint8_t) (MAL_massBlockCount[lun] >> 8);
  SCSI_readFormatCapacityData[7] = (uint8_t) (MAL_massBlockCount[lun]);

  SCSI_readFormatCapacityData[9] = (uint8_t) (MAL_massBlockSize[lun] >> 16);
  SCSI_readFormatCapacityData[10] = (uint8_t) (MAL_massBlockSize[lun] >> 8);
  SCSI_readFormatCapacityData[11] = (uint8_t) (MAL_massBlockSize[lun]);
  usb_mass_transfer_data_request(SCSI_readFormatCapacityData, SCSI_READ_FORMAT_CAPACITY_DATA_LEN);
}

void scsi_read_capacity10_cmd(uint8_t lun) {
  if (usb_mass_mal_get_status(lun)) {
    scsi_set_sense_data(CBW.bLUN, SCSI_NOT_READY, SCSI_MEDIUM_NOT_PRESENT);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_ENABLE);
    usb_mass_bot_abort(BOT_DIR_IN);
    return;
  }

  SCSI_readFormatCapacity10Data[0] = (uint8_t) ((MAL_massBlockCount[lun] - 1) >> 24);
  SCSI_readFormatCapacity10Data[1] = (uint8_t) ((MAL_massBlockCount[lun] - 1) >> 16);
  SCSI_readFormatCapacity10Data[2] = (uint8_t) ((MAL_massBlockCount[lun] - 1) >> 8);
  SCSI_readFormatCapacity10Data[3] = (uint8_t) (MAL_massBlockCount[lun] - 1);

  SCSI_readFormatCapacity10Data[4] = (uint8_t) (MAL_massBlockSize[lun] >> 24);
  SCSI_readFormatCapacity10Data[5] = (uint8_t) (MAL_massBlockSize[lun] >> 16);
  SCSI_readFormatCapacity10Data[6] = (uint8_t) (MAL_massBlockSize[lun] >> 8);
  SCSI_readFormatCapacity10Data[7] = (uint8_t) (MAL_massBlockSize[lun]);
  usb_mass_transfer_data_request(SCSI_readFormatCapacity10Data, SCSI_READ_FORMAT_CAPACITY10_DATA_LEN);
}

void scsi_read10_cmd(uint8_t lun, uint32_t lba, uint32_t blockNbr) {
  if (botState == BOT_STATE_IDLE) {
    if (!(scsi_address_management(CBW.bLUN, SCSI_READ10, lba, blockNbr))) /*address out of range*/ {
      return;
    }

    if ((CBW.bmFlags & 0x80) != 0) {
      botState = BOT_STATE_DATA_IN;
      usb_mass_mal_read_memory(lun, lba, blockNbr);
    } else {
      usb_mass_bot_abort(BOT_DIR_BOTH);
      scsi_set_sense_data(CBW.bLUN, SCSI_ILLEGAL_REQUEST, SCSI_INVALID_FIELED_IN_COMMAND);
      usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_ENABLE);
    }
    return;
  } else if (botState == BOT_STATE_DATA_IN) {
    usb_mass_mal_read_memory(lun, lba, blockNbr);
  }
}

void scsi_write10_cmd(uint8_t lun, uint32_t lba, uint32_t blockNbr) {
  if (botState == BOT_STATE_IDLE) {
    if (!(scsi_address_management(CBW.bLUN, SCSI_WRITE10, lba, blockNbr)))/*address out of range*/ {
      return;
    }

    if ((CBW.bmFlags & 0x80) == 0) {
      botState = BOT_STATE_DATA_OUT;
      SetEPRxStatus(USB_EP2, USB_EP_ST_RX_VAL);
    } else {
      usb_mass_bot_abort(BOT_DIR_IN);
      scsi_set_sense_data(CBW.bLUN, SCSI_ILLEGAL_REQUEST, SCSI_INVALID_FIELED_IN_COMMAND);
      usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_DISABLE);
    }
    return;
  } else if (botState == BOT_STATE_DATA_OUT) {
    usb_mass_mal_write_memory(lun, lba, blockNbr);
  }
}

void scsi_test_unit_ready_cmd(uint8_t lun) {
  if (usb_mass_mal_get_status(lun)) {
    scsi_set_sense_data(CBW.bLUN, SCSI_NOT_READY, SCSI_MEDIUM_NOT_PRESENT);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_ENABLE);
    usb_mass_bot_abort(BOT_DIR_IN);
    return;
  } else {
    usb_mass_bot_set_csw(BOT_CSW_CMD_PASSED, BOT_SEND_CSW_ENABLE);
  }
}

void scsi_verify10_cmd(uint8_t lun) {
  if ((CBW.dDataLength == 0) && !(CBW.CB[1] & SCSI_BLKVFY))/* BLKVFY not set*/ {
    usb_mass_bot_set_csw(BOT_CSW_CMD_PASSED, BOT_SEND_CSW_ENABLE);
  } else {
    usb_mass_bot_abort(BOT_DIR_BOTH);
    scsi_set_sense_data(CBW.bLUN, SCSI_ILLEGAL_REQUEST, SCSI_INVALID_FIELED_IN_COMMAND);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_DISABLE);
  }
}

void scsi_format_cmd(uint8_t lun) {
  if (usb_mass_mal_get_status(lun)) {
    scsi_set_sense_data(CBW.bLUN, SCSI_NOT_READY, SCSI_MEDIUM_NOT_PRESENT);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_ENABLE);
    usb_mass_bot_abort(BOT_DIR_IN);
    return;
  }
  usb_mass_mal_format();
  usb_mass_bot_set_csw(BOT_CSW_CMD_PASSED, BOT_SEND_CSW_ENABLE);
}

void scsi_set_sense_data(uint8_t lun, uint8_t sensKey, uint8_t asc) {
  SCSI_senseData[2] = sensKey;
  SCSI_senseData[12] = asc;
}

void scsi_invalid_cmd(uint8_t lun) {
  if (CBW.dDataLength == 0) {
    usb_mass_bot_abort(BOT_DIR_IN);
  } else {
    if ((CBW.bmFlags & 0x80) != 0) {
      usb_mass_bot_abort(BOT_DIR_IN);
    } else {
      usb_mass_bot_abort(BOT_DIR_BOTH);
    }
  }
  scsi_set_sense_data(CBW.bLUN, SCSI_ILLEGAL_REQUEST, SCSI_INVALID_COMMAND);
  usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_DISABLE);
}

uint8_t scsi_address_management(uint8_t lun, uint8_t cmd, uint32_t lba, uint32_t blockNbr) {

  if ((lba + blockNbr) > MAL_massBlockCount[lun]) {
    if (cmd == SCSI_WRITE10) {
      usb_mass_bot_abort(BOT_DIR_BOTH);
    }
    usb_mass_bot_abort(BOT_DIR_IN);
    scsi_set_sense_data(lun, SCSI_ILLEGAL_REQUEST, SCSI_ADDRESS_OUT_OF_RANGE);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_DISABLE);
    return (FALSE);
  }


  if (CBW.dDataLength != blockNbr * MAL_massBlockSize[lun]) {
    if (cmd == SCSI_WRITE10) {
      usb_mass_bot_abort(BOT_DIR_BOTH);
    } else {
      usb_mass_bot_abort(BOT_DIR_IN);
    }
    scsi_set_sense_data(CBW.bLUN, SCSI_ILLEGAL_REQUEST, SCSI_INVALID_FIELED_IN_COMMAND);
    usb_mass_bot_set_csw(BOT_CSW_CMD_FAILED, BOT_SEND_CSW_DISABLE);
    return (FALSE);
  }
  return (TRUE);
}