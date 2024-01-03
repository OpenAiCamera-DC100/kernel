/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "../halmac_efuse_88xx.h"
#include "../halmac_init_88xx.h"
#include "../halmac_common_88xx.h"

#include "halmac_efuse_8822e.h"
#include "halmac_8822e_cfg.h"
#include "halmac_common_8822e.h"
#include "halmac_init_8822e.h"

#if HALMAC_8822E_SUPPORT

#define RSVD_EFUSE_SIZE		16
#define RSVD_CS_EFUSE_SIZE	24
#define FEATURE_DUMP_PHY_EFUSE	HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE
#define FEATURE_DUMP_LOG_EFUSE	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE
#define FEATURE_DUMP_LOG_EFUSE_MASK	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK

static u8 bt_switch = 0;
static u16 efuse_ctrl_reg = REG_EFUSE_CTRL;

static enum halmac_cmd_construct_state
efuse_cmd_cnstr_state_8822e(struct halmac_adapter *adapter);

static enum halmac_ret_status
proc_dump_efuse_8822e(struct halmac_adapter *adapter,
		     enum halmac_efuse_read_cfg cfg);

static enum halmac_ret_status
read_hw_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u32 size,
		   u8 *map);

static enum halmac_ret_status
read_log_efuse_map_8822e(struct halmac_adapter *adapter, u8 *map);

static enum halmac_ret_status
proc_pg_efuse_by_map_8822e(struct halmac_adapter *adapter,
			  struct halmac_pg_efuse_info *info,
			  enum halmac_efuse_read_cfg cfg);

static enum halmac_ret_status
dump_efuse_fw_8822e(struct halmac_adapter *adapter);

static enum halmac_ret_status
dump_efuse_drv_8822e(struct halmac_adapter *adapter);

static enum halmac_ret_status
proc_write_log_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 value);

static enum halmac_ret_status
proc_write_log_efuse_word_8822e(struct halmac_adapter *adapter, u32 offset,
			       u16 value);

static enum halmac_ret_status
update_eeprom_mask_8822e(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask);

static enum halmac_ret_status
check_efuse_enough_8822e(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask);

static enum halmac_ret_status
proc_pg_efuse_8822e(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 word_en,
		   u8 pre_word_en, u32 eeprom_offset);

static enum halmac_ret_status
program_efuse_8822e(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 *updated_mask);

static void
mask_eeprom_8822e(struct halmac_adapter *adapter,
		 struct halmac_pg_efuse_info *info);

static void enable_efuse_sw_pwr_cut(struct halmac_adapter *adapter,
				    u8 is_write);

static void disable_efuse_sw_pwr_cut(struct halmac_adapter *adapter,
				     u8 is_write);
static enum halmac_ret_status
compare_version(struct halmac_adapter *adapter,
		struct halmac_pg_efuse_info *info, u32 ver_len);

/**
 * dump_efuse_map_8822e() - dump "physical" efuse map
 * @adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_efuse_map_8822e(struct halmac_adapter *adapter,
		    enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u8 *efuse_map;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	PLTFM_MSG_TRACE("dump_efuse_map_8822e, efuse_map_valid = %d\n", adapter->efuse_map_valid);

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.phy_efuse_map = 1;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}

	status = proc_dump_efuse_8822e(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse!! status = %d\n", status);
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;
		efuse_map = adapter->efuse_map;

		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc!!\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, efuse_size);
		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
#if 1
		PLTFM_MEMCPY(map, efuse_map, efuse_size);
#else
		PLTFM_MEMCPY(map, efuse_map, efuse_size - prtct_efuse_size);
		PLTFM_MEMCPY(map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     efuse_map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     prtct_efuse_size - RSVD_EFUSE_SIZE -
			     RSVD_CS_EFUSE_SIZE);
#endif
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,
				*proc_status, map, efuse_size);
		adapter->evnt.phy_efuse_map = 0;

		PLTFM_FREE(map, efuse_size);
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * dump_efuse_map_bt_8822e() - dump "BT physical" efuse map
 * @adapter : the adapter of halmac
 * @bank : bt efuse bank
 * @size : bt efuse map size. get from halmac_get_efuse_size API
 * @map : bt efuse map
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_efuse_map_bt_8822e(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank, u32 size, u8 *map)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->hw_cfg_info.bt_efuse_size != size)
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;

	if (bank >= HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}
	bt_switch = 1;

	status = read_hw_efuse_8822e(adapter, BT_START_OFFSET, size, map);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]read hw efuse\n");
		return status;
	}
	
	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}
	bt_switch = 0;

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_efuse_bt_8822e() - write "BT physical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : Write value
 * @map : bt efuse map
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_efuse_bt_8822e(struct halmac_adapter *adapter, u32 offset, u8 value,
		    enum halmac_efuse_bank bank)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (offset >= adapter->hw_cfg_info.bt_efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (bank > HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = switch_efuse_bank_8822e(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}
	bt_switch = 1;

	status = write_hw_efuse_8822e(adapter, offset + BT_START_OFFSET, value);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]write efuse\n");
		return status;
	}
	
	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}
	bt_switch = 0;

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_efuse_bt_8822e() - read "BT physical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : 1 byte efuse value
 * @bank : efuse bank
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_efuse_bt_8822e(struct halmac_adapter *adapter, u32 offset, u8 *value,
		   enum halmac_efuse_bank bank)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (offset >= adapter->hw_cfg_info.bt_efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (bank > HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = switch_efuse_bank_8822e(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}
	bt_switch = 1;

	status = read_efuse_8822e(adapter, offset + BT_START_OFFSET, 1, value);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]read efuse\n");
		return status;
	}

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		bt_switch = 0;
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}
	bt_switch = 0;

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_efuse_available_size_8822e() - get efuse available size
 * @adapter : the adapter of halmac
 * @size : physical efuse available size
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_efuse_available_size_8822e(struct halmac_adapter *adapter, u32 *size)
{
	enum halmac_ret_status status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	status = dump_log_efuse_map_8822e(adapter, HALMAC_EFUSE_R_DRV);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	*size = adapter->hw_cfg_info.efuse_size - adapter->efuse_end;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * dump_log_efuse_map_8822e() - dump "logical" efuse map
 * @adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_log_efuse_map_8822e(struct halmac_adapter *adapter,
			enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.log_efuse_map = 1;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_dump_efuse_8822e(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse\n");
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;

		map = (u8 *)PLTFM_MALLOC(size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, size);

		if (eeprom_parser_8822e(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,
				*proc_status, map, size);
		adapter->evnt.log_efuse_map = 0;

		PLTFM_FREE(map, size);
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
dump_log_efuse_mask_8822e(struct halmac_adapter *adapter,
			 enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.log_efuse_mask = 1;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_dump_efuse_8822e(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse\n");
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;

		map = (u8 *)PLTFM_MALLOC(size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, size);

		if (eeprom_mask_parser_8822e(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK,
				*proc_status, map, size);
		adapter->evnt.log_efuse_mask = 0;

		PLTFM_FREE(map, size);
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_logical_efuse_8822e() - read logical efuse map 1 byte
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : 1 byte efuse value
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_logical_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 *value)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}
	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	map = (u8 *)PLTFM_MALLOC(size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, size);

	status = read_log_efuse_map_8822e(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, size);
		return status;
	}

	*value = *(map + offset);

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, size);
		return HALMAC_RET_ERROR_STATE;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	PLTFM_FREE(map, size);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_log_efuse_8822e() - write "logical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : value
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_log_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_write_log_efuse_8822e(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write logical efuse\n");
		return status;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_log_efuse_word_8822e() - write "logical" efuse offset word
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : value
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_log_efuse_word_8822e(struct halmac_adapter *adapter, u32 offset, u16 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_write_log_efuse_word_8822e(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write logical efuse\n");
		return status;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * pg_efuse_by_map_8822e() - pg logical efuse by map
 * @adapter : the adapter of halmac
 * @info : efuse map information
 * @cfg : dump efuse method
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pg_efuse_by_map_8822e(struct halmac_adapter *adapter,
		     struct halmac_pg_efuse_info *info,
		     enum halmac_efuse_read_cfg cfg)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (info->efuse_map_size != adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]map size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((info->efuse_map_size & 0xF) > 0) {
		PLTFM_MSG_ERR("[ERR]not multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (info->efuse_mask_size != info->efuse_map_size >> 4) {
		PLTFM_MSG_ERR("[ERR]mask size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (!info->efuse_map) {
		PLTFM_MSG_ERR("[ERR]map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (!info->efuse_mask) {
		PLTFM_MSG_ERR("[ERR]mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_pg_efuse_by_map_8822e(adapter, info, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]pg efuse\n");
		return status;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mask_log_efuse_8822e() - mask logical efuse
 * @adapter : the adapter of halmac
 * @info : efuse map information
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mask_log_efuse_8822e(struct halmac_adapter *adapter,
		    struct halmac_pg_efuse_info *info)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (info->efuse_map_size != adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]map size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((info->efuse_map_size & 0xF) > 0) {
		PLTFM_MSG_ERR("[ERR]not multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (info->efuse_mask_size != info->efuse_map_size >> 4) {
		PLTFM_MSG_ERR("[ERR]mask size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (!info->efuse_map) {
		PLTFM_MSG_ERR("[ERR]map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (!info->efuse_mask) {
		PLTFM_MSG_ERR("[ERR]mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	mask_eeprom_8822e(adapter, info);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_cmd_construct_state
efuse_cmd_cnstr_state_8822e(struct halmac_adapter *adapter)
{
	return adapter->halmac_state.efuse_state.cmd_cnstr_state;
}

enum halmac_ret_status
switch_efuse_bank_8822e(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank)
{
	/*u8 reg_value;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (!bt_switch) {
		if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_ERROR_STATE;
	}

	reg_value = HALMAC_REG_R8(REG_LDO_EFUSE_CTRL + 1);

	if (bank == (reg_value & (BIT(0) | BIT(1))))
		return HALMAC_RET_SUCCESS;

	reg_value &= ~(BIT(0) | BIT(1));
	reg_value |= bank;
	HALMAC_REG_W8(REG_LDO_EFUSE_CTRL + 1, reg_value);

	reg_value = HALMAC_REG_R8(REG_LDO_EFUSE_CTRL + 1);
	if ((reg_value & (BIT(0) | BIT(1))) != bank)
		return HALMAC_RET_SWITCH_EFUSE_BANK_FAIL;*/

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_dump_efuse_8822e(struct halmac_adapter *adapter,
		     enum halmac_efuse_read_cfg cfg)
{
	u32 h2c_init;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	PLTFM_MSG_TRACE("proc_dump_efuse_8822e, efuse_map_valid = %d\n", adapter->efuse_map_valid);

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (cfg == HALMAC_EFUSE_R_AUTO) {
		h2c_init = HALMAC_REG_R32(REG_H2C_PKT_READADDR);
		if (adapter->halmac_state.dlfw_state == HALMAC_DLFW_NONE ||
		    h2c_init == 0)
			status = dump_efuse_drv_8822e(adapter);
		else
			status = dump_efuse_fw_8822e(adapter);
	} else if (cfg == HALMAC_EFUSE_R_FW) {
		status = dump_efuse_fw_8822e(adapter);
	} else {
		status = dump_efuse_drv_8822e(adapter);
	}

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efsue drv/fw\n");
		return status;
	}

	return status;
}

enum halmac_ret_status
cnv_efuse_state_8822e(struct halmac_adapter *adapter,
		     enum halmac_cmd_construct_state dest_state)
{
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	if (state->cmd_cnstr_state != HALMAC_CMD_CNSTR_IDLE &&
	    state->cmd_cnstr_state != HALMAC_CMD_CNSTR_BUSY &&
	    state->cmd_cnstr_state != HALMAC_CMD_CNSTR_H2C_SENT)
		return HALMAC_RET_ERROR_STATE;

	if (state->cmd_cnstr_state == dest_state)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_CMD_CNSTR_BUSY) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_H2C_SENT) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_IDLE)
			return HALMAC_RET_ERROR_STATE;
	}

	state->cmd_cnstr_state = dest_state;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
read_hw_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u32 size,
		   u8 *map)
{
	u32 addr;
	u32 tmp32;
	u32 cnt;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	enable_efuse_sw_pwr_cut(adapter, 0);

	for (addr = offset; addr < offset + size; addr++) {
		HALMAC_REG_W32(efuse_ctrl_reg,
			       BIT_EF_ADDR_V1(addr) & ~BIT_EF_RDY);

		cnt = 10000;
		do {
			PLTFM_DELAY_US(1);
			tmp32 = HALMAC_REG_R32(efuse_ctrl_reg);
			cnt--;
			if (cnt == 0) {
				PLTFM_MSG_ERR("[ERR]read\n");
				return HALMAC_RET_EFUSE_R_FAIL;
			}
		} while ((tmp32 & BIT_EF_RDY) == 0);

		*(map + addr - offset) = (u8)(BIT_GET_EF_DATA_V1(tmp32));
	}

	disable_efuse_sw_pwr_cut(adapter, 0);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
write_hw_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	u8 value_read = 0;
	u32 value32;
	u32 tmp32;
	u32 cnt;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
	adapter->efuse_map_valid = 0;
	PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

	value32 = BIT_EF_MODE_SEL(0x2);
	value32 = BIT_SET_EF_ADDR_V1(value32, offset);
	value32 = BIT_SET_EF_DATA_V1(value32, value);
	HALMAC_REG_W32(efuse_ctrl_reg, value32 & ~BIT_EF_RDY);

	PLTFM_MSG_TRACE("write_hw_efuse offset = 0x%x val = 0x%x====>\n", offset, value);

	cnt = 10000;
	do {
		PLTFM_DELAY_US(1);
		tmp32 = HALMAC_REG_R32(efuse_ctrl_reg);
		cnt--;
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]write!!\n");
			return HALMAC_RET_EFUSE_W_FAIL;
		}
	} while ((tmp32 & BIT_EF_RDY) == 0);

	if (adapter->efuse_auto_check_en == 1) {
		if (read_hw_efuse_8822e(adapter, offset, 1, &value_read) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_EFUSE_R_FAIL;
		if (value_read != value) {
			PLTFM_MSG_ERR("[ERR]efuse compare\n");
			return HALMAC_RET_EFUSE_W_FAIL;
		}
	}

	PLTFM_MSG_TRACE("<=====write_hw_efuse\n");

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
eeprom_parser_8822e(struct halmac_adapter *adapter, u8 *phy_map, u8 *log_map)
{
	u8 i;
	u8 value8;
	u8 blk_idx;
	u8 word_en;
	u8 valid;
	u8 hdr;
	u8 hdr2 = 0;
	u32 eeprom_idx;
	u32 efuse_idx = SEC_CTRL_EFUSE_SIZE_8822E;
	struct halmac_hw_cfg_info *hw_info = &adapter->hw_cfg_info;

	PLTFM_MEMSET(log_map, 0xFF, adapter->hw_cfg_info.eeprom_size);

	do {
		if (efuse_idx >= hw_info->efuse_size)
			break;

		value8 = *(phy_map + efuse_idx);
		hdr = value8;
		if (hdr == 0xff)
			break;
		efuse_idx++;

		if (bt_switch) {
			if ((hdr & 0xF) != 0xF) { /* 1byte entry */
				blk_idx = (hdr & 0xF0) >> 4;
				word_en = hdr & 0x0F;
			} else { /* 2byte entry*/
				value8 = *(phy_map + efuse_idx);
				hdr2 = value8;
				if (hdr2 == 0xff)
					break;

				blk_idx = (((hdr2 & 0xF0) >> 4) << 3) +
					  (((hdr & 0xF0) >> 4) >> 1);
				word_en = hdr2 & 0x0F;

				efuse_idx++;
			}
		} else {
			value8 = *(phy_map + efuse_idx);
			hdr2 = value8;
			if (hdr2 == 0xff)
				break;

			blk_idx = ((hdr2 & 0xF0) >> 4) | ((hdr & 0x0F) << 4);
			word_en = hdr2 & 0x0F;

			efuse_idx++;
		}

		if (efuse_idx >= hw_info->efuse_size - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (i = 0; i < 4; i++) {
			valid = (u8)((~(word_en >> i)) & BIT(0));
			if (valid == 1) {
				eeprom_idx = (blk_idx << 3) + (i << 1);

				if ((eeprom_idx + 1) > hw_info->eeprom_size) {
					PLTFM_MSG_ERR("[ERR]efuse idx:0x%X\n",
						      efuse_idx - 1);

					PLTFM_MSG_ERR("[ERR]read hdr:0x%X\n",
						      hdr);

					PLTFM_MSG_ERR("[ERR]rad hdr2:0x%X\n",
						      hdr2);

					return HALMAC_RET_EEPROM_PARSING_FAIL;
				}

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				eeprom_idx++;
				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size - 1)
					return HALMAC_RET_EEPROM_PARSING_FAIL;

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size)
					return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
		}
	} while (1);

	adapter->efuse_end = efuse_idx;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
eeprom_mask_parser_8822e(struct halmac_adapter *adapter, u8 *phy_map,
			u8 *log_mask)
{
	u8 i;
	u8 value8;
	u8 blk_idx;
	u8 word_en;
	u8 valid;
	u8 hdr;
	u8 hdr2 = 0;
	u32 eeprom_idx;
	u32 efuse_idx = SEC_CTRL_EFUSE_SIZE_8822E;
	struct halmac_hw_cfg_info *hw_info = &adapter->hw_cfg_info;

	PLTFM_MEMSET(log_mask, 0xFF, hw_info->eeprom_size);

	do {
		if (efuse_idx >= hw_info->efuse_size)
			break;

		value8 = *(phy_map + efuse_idx);
		hdr = value8;
		if (hdr == 0xff)
			break;

		value8 = *(phy_map + efuse_idx + 1);
		hdr2 = value8;
		if (hdr2 == 0xff)
			break;

		blk_idx = ((hdr2 & 0xF0) >> 4) | ((hdr & 0x0F) << 4);
		word_en = hdr2 & 0x0F;

		efuse_idx += 2;

		if (efuse_idx >= hw_info->efuse_size - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (i = 0; i < 4; i++) {
			valid = (u8)((~(word_en >> i)) & BIT(0));
			if (valid == 1) {
				eeprom_idx = (blk_idx << 3) + (i << 1);

				if ((eeprom_idx + 1) > hw_info->eeprom_size) {
					PLTFM_MSG_ERR("[ERR]efuse idx:0x%X\n",
						      efuse_idx - 1);

					PLTFM_MSG_ERR("[ERR]read hdr:0x%X\n",
						      hdr);

					PLTFM_MSG_ERR("[ERR]rad hdr2:0x%X\n",
						      hdr2);

					return HALMAC_RET_EEPROM_PARSING_FAIL;
				}

				*(log_mask + eeprom_idx) = 0x00;

				eeprom_idx++;
				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size - 1)
					return HALMAC_RET_EEPROM_PARSING_FAIL;

				*(log_mask + eeprom_idx) = 0x00;

				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size)
					return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
		}
	} while (1);

	adapter->efuse_end = efuse_idx;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
read_log_efuse_map_8822e(struct halmac_adapter *adapter, u8 *map)
{
	u8 *local_map = NULL;
	u32 efuse_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->efuse_map_valid == 0) {
		efuse_size = adapter->hw_cfg_info.efuse_size;

		local_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!local_map) {
			PLTFM_MSG_ERR("[ERR]local map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		status = read_efuse_8822e(adapter, 0, efuse_size, local_map);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]read efuse\n");
			PLTFM_FREE(local_map, efuse_size);
			return status;
		}

		if (!adapter->efuse_map) {
			adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
			if (!adapter->efuse_map) {
				PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
				PLTFM_FREE(local_map, efuse_size);
				return HALMAC_RET_MALLOC_FAIL;
			}
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, local_map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(local_map, efuse_size);
	}

	if (eeprom_parser_8822e(adapter, adapter->efuse_map, map) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_EEPROM_PARSING_FAIL;

	return status;
}

static enum halmac_ret_status
proc_pg_efuse_by_map_8822e(struct halmac_adapter *adapter,
			  struct halmac_pg_efuse_info *info,
			  enum halmac_efuse_read_cfg cfg)
{
	u8 *updated_mask = NULL;
	u8 *updated_map = NULL;
	u32 map_size = adapter->hw_cfg_info.eeprom_size;
	u32 mask_size = adapter->hw_cfg_info.eeprom_size >> 4;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = compare_version(adapter, info, PG_VER_LEN_8822E);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]efuse map & mask version unmatched");
		return status;
	}		

	updated_mask = (u8 *)PLTFM_MALLOC(mask_size);
	if (!updated_mask) {
		PLTFM_MSG_ERR("[ERR]malloc updated mask\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(updated_mask, 0x00, mask_size);

	status = update_eeprom_mask_8822e(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]update eeprom mask\n");
		PLTFM_FREE(updated_mask, mask_size);
		return status;
	}

	status = check_efuse_enough_8822e(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]chk efuse enough\n");
		PLTFM_FREE(updated_mask, mask_size);
		return status;
	}

	status = program_efuse_8822e(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]pg efuse\n");
		PLTFM_FREE(updated_mask, mask_size);
		return status;
	}

	PLTFM_FREE(updated_mask, mask_size);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
dump_efuse_drv_8822e(struct halmac_adapter *adapter)
{
	u8 *map = NULL;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;

	if (!adapter->efuse_map) {
		adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!adapter->efuse_map) {
			PLTFM_MSG_ERR("[ERR]malloc adapter map!!\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return HALMAC_RET_MALLOC_FAIL;
		}
	}

	PLTFM_MSG_TRACE("dump_efuse_drv_8822e, efuse_map_valid = %d\n", adapter->efuse_map_valid);

	if (adapter->efuse_map_valid == 0) {
		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		if (read_hw_efuse_8822e(adapter, 0, efuse_size, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, efuse_size);
			return HALMAC_RET_EFUSE_R_FAIL;
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(map, efuse_size);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
dump_efuse_fw_8822e(struct halmac_adapter *adapter)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	hdr_info.sub_cmd_id = SUB_CMD_ID_DUMP_PHYSICAL_EFUSE;
	hdr_info.content_size = 0;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.efuse_state.seq_num = seq_num;

	if (!adapter->efuse_map) {
		adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!adapter->efuse_map) {
			PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return HALMAC_RET_MALLOC_FAIL;
		}
	}

	if (adapter->efuse_map_valid == 0) {
		status = send_h2c_pkt_88xx(adapter, h2c_buf);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]send h2c pkt\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_write_log_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	u8 byte1;
	u8 byte2;
	u8 blk;
	u8 blk_idx;
	u8 hdr;
	u8 hdr2 = 0;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	u32 end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	status = read_log_efuse_map_8822e(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	if (*(map + offset) == value) {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_SUCCESS;
	}

	end = adapter->efuse_end;

	if (bt_switch) {
		if (offset < BT_1B_ENTRY_SIZE) { /* 1 byte entry */
			blk = (u8)(offset >> 3);
			blk_idx = (u8)((offset & (8 - 1)) >> 1);

			hdr = (u8)((blk << 4) +
				   ((0x1 << blk_idx) ^ 0x0F));
		} else { /* 2 byte entry */
			blk = (u8)(offset >> 3 >> 3); /* large section */
			blk_idx = (u8)(((offset >> 3) & (8 - 1)) << 1);
			hdr = (u8)((blk_idx << 4) + 0xF);

			blk_idx = (u8)((offset & (8 - 1)) >> 1);
			hdr2 = (u8)((blk << 4) +
				    ((0x1 << blk_idx) ^ 0x0F));
		}
	} else {
		blk = (u8)(offset >> 3);
		blk_idx = (u8)((offset & (8 - 1)) >> 1);

		hdr = ((blk & 0xF0) >> 4) | 0x30;
		hdr2 = (u8)(((blk & 0x0F) << 4) +
			    ((0x1 << blk_idx) ^ 0x0F));
		PLTFM_MSG_TRACE("[OTP]hdr = 0x%x hdr2 = 0x%x\n", hdr, hdr2);
	}

	if ((offset & 1) == 0) {
		byte1 = value;
		byte2 = *(map + offset + 1);
	} else {
		byte1 = *(map + offset - 1);
		byte2 = value;
	}
	PLTFM_MSG_TRACE("[OTP]byte1 = 0x%x byte2 = 0x%x\n", byte1, byte2);

	if (!bt_switch || offset >= BT_1B_ENTRY_SIZE) {
		if (adapter->hw_cfg_info.efuse_size <= 4 + end) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EFUSE_NOT_ENOUGH;
		}
	} else {
		if (adapter->hw_cfg_info.efuse_size <= 3 + end) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EFUSE_NOT_ENOUGH;
		}
	}	

	enable_efuse_sw_pwr_cut(adapter, 1);

	status = write_hw_efuse_8822e(adapter, end, hdr);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}
	end++;

	if (!bt_switch || offset >= BT_1B_ENTRY_SIZE) {
		status = write_hw_efuse_8822e(adapter, end, hdr2);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}
		end++;
	}

	status = write_hw_efuse_8822e(adapter, end, byte1);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	status = write_hw_efuse_8822e(adapter, end + 1, byte2);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}
	
	disable_efuse_sw_pwr_cut(adapter, 1);

	PLTFM_FREE(map, eeprom_size);
	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_write_log_efuse_word_8822e(struct halmac_adapter *adapter, u32 offset,
			       u16 value)
{
	u8 byte1;
	u8 byte2;
	u8 blk;
	u8 blk_idx;
	u8 hdr;
	u8 hdr2 = 0;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u32 end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	status = read_log_efuse_map_8822e(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	if (*(u16 *)(map + offset) == value) {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_SUCCESS;
	}

	end = adapter->efuse_end;

	if (bt_switch) {
		if (offset < BT_1B_ENTRY_SIZE) { /* 1 byte entry */
			blk = (u8)(offset >> 3);
			blk_idx = (u8)((offset & (8 - 1)) >> 1);

			hdr = (u8)((blk << 4) +
				   ((0x1 << blk_idx) ^ 0x0F));
		} else { /* 2 byte entry */
			blk = (u8)(offset >> 3 >> 3); /* large section */
			blk_idx = (u8)(((offset >> 3) & (8 - 1)) << 1);
			hdr = (u8)((blk_idx << 4) + 0xF);

			blk_idx = (u8)((offset & (8 - 1)) >> 1);
			hdr2 = (u8)((blk << 4) +
				    ((0x1 << blk_idx) ^ 0x0F));
		}
	} else {
		blk = (u8)(offset >> 3);
		blk_idx = (u8)((offset & (8 - 1)) >> 1);

		hdr = ((blk & 0xF0) >> 4) | 0x30;
		hdr2 = (u8)(((blk & 0x0F) << 4) + ((0x1 << blk_idx) ^ 0x0F));
	}

	if ((offset & 1) == 0) {
		byte1 = (u8)(value & 0xFF);
		byte2 = (u8)((value >> 8) & 0xFF);
	} else {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_ADR_NOT_ALIGN;
	}

	if (adapter->hw_cfg_info.efuse_size <= 4 + end) {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_EFUSE_NOT_ENOUGH;
	}

	enable_efuse_sw_pwr_cut(adapter, 1);

	status = write_hw_efuse_8822e(adapter, end, hdr);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}
	end++;

	if (!bt_switch || offset >= BT_1B_ENTRY_SIZE) {
		status = write_hw_efuse_8822e(adapter, end, hdr2);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}
		end++;
	}

	status = write_hw_efuse_8822e(adapter, end, byte1);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	status = write_hw_efuse_8822e(adapter, end + 1, byte2);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	disable_efuse_sw_pwr_cut(adapter, 1);

	PLTFM_FREE(map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
read_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u32 size, u8 *map)
{
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (adapter->efuse_map_valid == 1) {
		PLTFM_MEMCPY(map, adapter->efuse_map + offset, size);
	} else {
		if (read_hw_efuse_8822e(adapter, offset, size, map) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_EFUSE_R_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_eeprom_mask_8822e(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 *map = NULL;
	u8 *mask_map = NULL;
	u8 clr_bit = 0;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u8 *map_pg;
	u8 *efuse_mask;
	u16 i;
	u16 j;
	u16 map_offset;
	u16 mask_offset;
	u16 efuse_mask_size, efuse_map_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	PLTFM_MEMSET(updated_mask, 0x00, info->efuse_mask_size);

	status = read_log_efuse_map_8822e(adapter, map);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	
	/*log mask*/
	mask_map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!mask_map) {
		PLTFM_MSG_ERR("[ERR]malloc mask map\n");
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(mask_map, 0xFF, eeprom_size);

	status = eeprom_mask_parser_8822e(adapter, adapter->efuse_map, mask_map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(mask_map, eeprom_size);
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	map_pg = info->efuse_map;
	efuse_mask = info->efuse_mask;
	efuse_mask_size = (u16)info->efuse_mask_size;
	efuse_map_size = (u16)info->efuse_map_size;

	for (i = 0; i < efuse_mask_size; i++)
		*(updated_mask + i) = *(efuse_mask + i);

	for (i = 0; i < efuse_map_size; i += 16) {
		for (j = 0; j < 16; j += 2) {
			map_offset = i + j;
			mask_offset = i >> 4;
			if (*(u16 *)(map_pg + map_offset) ==
			    *(u16 *)(map + map_offset) &&
			    *(mask_map + map_offset) == 0x00) {
				switch (j) {
				case 0:
					clr_bit = BIT(4);
					break;
				case 2:
					clr_bit = BIT(5);
					break;
				case 4:
					clr_bit = BIT(6);
					break;
				case 6:
					clr_bit = BIT(7);
					break;
				case 8:
					clr_bit = BIT(0);
					break;
				case 10:
					clr_bit = BIT(1);
					break;
				case 12:
					clr_bit = BIT(2);
					break;
				case 14:
					clr_bit = BIT(3);
					break;
				default:
					break;
				}
				*(updated_mask + mask_offset) &= ~clr_bit;
			}
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return status;
}

static enum halmac_ret_status
check_efuse_enough_8822e(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 pre_word_en;
	u16 i;
	u16 j;
	u16 efuse_map_size = (u16)info->efuse_map_size;
	u32 eeprom_offset;
	u32 pg_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	for (i = 0; i < info->efuse_map_size; i = i + 8) {
		eeprom_offset = i;

		if ((eeprom_offset & 7) > 0)
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
		else
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);

		if (pre_word_en > 0) {
			pg_num += 2;

			for (j = 0; j < 4; j++) {
				if (((pre_word_en >> j) & 0x1) > 0)
					pg_num += 2;
			}
		}
	}

	if (adapter->hw_cfg_info.efuse_size <= pg_num + adapter->efuse_end)
		return HALMAC_RET_EFUSE_NOT_ENOUGH;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_pg_efuse_8822e(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 word_en,
		   u8 pre_word_en, u32 eeprom_offset)
{
	u8 blk;
	u8 hdr, hdr2;
	u16 i;
	u32 efuse_end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	efuse_end = adapter->efuse_end;

	blk = (u8)(eeprom_offset >> 3);
	hdr = ((blk & 0xF0) >> 4) | 0x30;
	hdr2 = (u8)(((blk & 0x0F) << 4) + word_en);

	status = write_hw_efuse_8822e(adapter, efuse_end, hdr);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse\n");
		return status;
	}

	status = write_hw_efuse_8822e(adapter, efuse_end + 1, hdr2);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse(+1)\n");
		return status;
	}
	efuse_end += 2;

	for (i = 0; i < 4; i++) {
		if (((pre_word_en >> i) & 0x1) > 0) {
			status = write_hw_efuse_8822e(adapter, efuse_end,
						     *(info->efuse_map +
						     eeprom_offset +
						     (i << 1)));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)\n");
				return status;
			}
			status = write_hw_efuse_8822e(adapter, efuse_end + 1,
						     *(info->efuse_map +
						     eeprom_offset + (i << 1)
						     + 1));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)+1\n");
				return status;
			}
			efuse_end = efuse_end + 2;
		}
	}
	adapter->efuse_end = efuse_end;
	return status;
}

static enum halmac_ret_status
program_efuse_8822e(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 pre_word_en;
	u8 word_en;
	u16 i;
	u16 efuse_map_size = (u16)info->efuse_map_size;
	u32 eeprom_offset;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	enable_efuse_sw_pwr_cut(adapter, 1);

	for (i = 0; i < efuse_map_size; i = i + 8) {
		eeprom_offset = i;

		if (((eeprom_offset >> 3) & 1) > 0) {
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
			word_en = pre_word_en ^ 0x0F;
		} else {
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);
			word_en = pre_word_en ^ 0x0F;
		}

		if (pre_word_en > 0) {
			status = proc_pg_efuse_8822e(adapter, info,
						    word_en,
						    pre_word_en,
						    eeprom_offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]extend efuse");
				return status;
			}
		}
	}

	disable_efuse_sw_pwr_cut(adapter, 1);

	return status;
}

static void
mask_eeprom_8822e(struct halmac_adapter *adapter,
		 struct halmac_pg_efuse_info *info)
{
	u8 pre_word_en;
	u8 *updated_mask;
	u8 *efuse_map;
	u16 i;
	u16 j;
	u16 efuse_map_size = (u16)info->efuse_map_size;
	u32 offset;

	updated_mask = info->efuse_mask;
	efuse_map = info->efuse_map;

	for (i = 0; i < efuse_map_size; i = i + 8) {
		offset = i;

		if (((offset >> 3) & 1) > 0)
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
		else
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);

		for (j = 0; j < 4; j++) {
			if (((pre_word_en >> j) & 0x1) == 0) {
				*(efuse_map + offset + (j << 1)) = 0xFF;
				*(efuse_map + offset + (j << 1) + 1) = 0xFF;
			}
		}
	}
}

enum halmac_ret_status
get_efuse_data_8822e(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seg_id;
	u8 seg_size;
	u8 seq_num;
	u8 fw_rc;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)EFUSE_DATA_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	seg_id = (u8)EFUSE_DATA_GET_SEGMENT_ID(buf);
	seg_size = (u8)EFUSE_DATA_GET_SEGMENT_SIZE(buf);
	if (seg_id == 0)
		adapter->efuse_seg_size = seg_size;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
	PLTFM_MEMCPY(adapter->efuse_map + seg_id * adapter->efuse_seg_size,
		     buf + C2H_DATA_OFFSET_88XX, seg_size);
	PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

	if (EFUSE_DATA_GET_END_SEGMENT(buf) == 0) {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = state->fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		if (adapter->evnt.phy_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_PHY_EFUSE,
					proc_status, adapter->efuse_map,
					adapter->hw_cfg_info.efuse_size);
			adapter->evnt.phy_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_map == 1) {
			if (eeprom_parser_8822e(adapter, adapter->efuse_map,
					       map) != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE, proc_status,
					map, eeprom_size);
			adapter->evnt.log_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_mask == 1) {
			if (eeprom_mask_parser_8822e(adapter, adapter->efuse_map,
						    map)
						    != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE_MASK,
					proc_status, map, eeprom_size);
			adapter->evnt.log_efuse_mask = 0;
		}

	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;

		if (adapter->evnt.phy_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_PHY_EFUSE, proc_status,
					&state->fw_rc, 1);
			adapter->evnt.phy_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE, proc_status,
					&state->fw_rc, 1);
			adapter->evnt.log_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_mask == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE_MASK,
					proc_status, &state->fw_rc, 1);
			adapter->evnt.log_efuse_mask = 0;
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_phy_efuse_status_8822e(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < efuse_size) {
			*size = efuse_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = efuse_size;

		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, efuse_size);
		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
#if 1
		PLTFM_MEMCPY(map, adapter->efuse_map, efuse_size);
#else
		PLTFM_MEMCPY(map, adapter->efuse_map,
			     efuse_size - prtct_efuse_size);
		PLTFM_MEMCPY(map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     adapter->efuse_map + efuse_size -
			     prtct_efuse_size + RSVD_CS_EFUSE_SIZE,
			     prtct_efuse_size - RSVD_EFUSE_SIZE -
			     RSVD_CS_EFUSE_SIZE);
#endif
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, efuse_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_log_efuse_status_8822e(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < eeprom_size) {
			*size = eeprom_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = eeprom_size;

		map = (u8 *)PLTFM_MALLOC(eeprom_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, eeprom_size);

		if (eeprom_parser_8822e(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, eeprom_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_log_efuse_mask_status_8822e(struct halmac_adapter *adapter,
				    enum halmac_cmd_process_status *proc_status,
				    u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < eeprom_size) {
			*size = eeprom_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = eeprom_size;

		map = (u8 *)PLTFM_MALLOC(eeprom_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, eeprom_size);

		if (eeprom_mask_parser_8822e(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, eeprom_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_h2c_ack_phy_efuse_8822e(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num = 0;
	u8 fw_rc;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	return HALMAC_RET_SUCCESS;
}

/**
 * write_wifi_phy_efuse_8822e() - write wifi physical efuse
 * @adapter : the adapter of halmac
 * @offset : the efuse offset to be written
 * @value : the value to be written
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_wifi_phy_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.efuse_size +
	    adapter->hw_cfg_info.prtct_efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}
	
	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	enable_efuse_sw_pwr_cut(adapter, 1);
	status = write_hw_efuse_8822e(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write physical efuse\n");
		return status;
	}
	disable_efuse_sw_pwr_cut(adapter, 1);

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_wifi_phy_efuse_8822e() - read wifi physical efuse
 * @adapter : the adapter of halmac
 * @offset : the efuse offset to be read
 * @size : the length to be read
 * @value : pointer to the pre-allocated space where
 the efuse content is to be copied
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_wifi_phy_efuse_8822e(struct halmac_adapter *adapter, u32 offset, u32 size,
			 u8 *value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;
	u32 wlan_efuse_size;

	PLTFM_MSG_TRACE("read_wifi_phy_efuse_8822e, efuse_map_valid = %d\n",
			adapter->efuse_map_valid);

	wlan_efuse_size = adapter->hw_cfg_info.efuse_size +
			  adapter->hw_cfg_info.prtct_efuse_size;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= wlan_efuse_size || offset + size > wlan_efuse_size) {
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_8822e(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	status = switch_efuse_bank_8822e(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = read_hw_efuse_8822e(adapter, offset, size, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read hw efuse\n");
		return status;
	}

	if (cnv_efuse_state_8822e(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static void enable_efuse_sw_pwr_cut(struct halmac_adapter *adapter,
				    u8 is_write)
{
	u16 val16;
	u8 val8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (is_write)
		HALMAC_REG_W8(REG_PMC_DBG_CTRL2 + 3, UNLOCK_CODE);

	val8 = HALMAC_REG_R8(REG_PMC_DBG_CTRL2);
	HALMAC_REG_W8(REG_PMC_DBG_CTRL2,
		      val8 | BIT_SYSON_DIS_PMCREG_WRMSK);

	val16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, val16 | BIT_PWC_EV2EF_S_8822E);
	PLTFM_DELAY_US(1000);

	val16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, val16 | BIT_PWC_EV2EF_B_8822E);

	val16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, val16 & ~(BIT_ISO_EB2CORE));
}

static void disable_efuse_sw_pwr_cut(struct halmac_adapter *adapter,
				     u8 is_write)
{
	u16 value16;
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	value16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, value16 | BIT_ISO_EB2CORE);

	value16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, value16 & ~(BIT_PWC_EV2EF_B_8822E));

	PLTFM_DELAY_US(1000);

	value16 = HALMAC_REG_R16(REG_SYS_ISO_CTRL);
	HALMAC_REG_W16(REG_SYS_ISO_CTRL, value16 & ~(BIT_PWC_EV2EF_S_8822E));

	if (is_write)
		HALMAC_REG_W8(REG_PMC_DBG_CTRL2 + 3, 0x00);

	value8 = HALMAC_REG_R8(REG_PMC_DBG_CTRL2);
	HALMAC_REG_W8(REG_PMC_DBG_CTRL2,
		      value8 & ~BIT_SYSON_DIS_PMCREG_WRMSK);
}

static enum halmac_ret_status
compare_version(struct halmac_adapter *adapter,
		struct halmac_pg_efuse_info *info, u32 ver_len)
{
	u8 *map = info->efuse_map;
	u8 *mask = info->efuse_mask;
	u32 map_size = info->efuse_map_size;
	u32 mask_size = info->efuse_mask_size;
	u32 i = 0;

	for (i = 0; i < ver_len; i++) {
		if (*(map + map_size + i) != *(mask + mask_size + i))
			return HALMAC_RET_EFUSE_VER_ERR;
	}
	return HALMAC_RET_SUCCESS;
}

/**
 * switch_ctrl_reg_8822e() - switch efuse control reg to 0x1030 or 0x30
 * @adapter : the adapter of halmac
 * Author : Eva
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
switch_ctrl_reg_8822e(struct halmac_adapter *adapter, u8 switch_reg)
{
	efuse_ctrl_reg = switch_reg ? 0x1030 : REG_EFUSE_CTRL;

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_8822E_SUPPORT */
