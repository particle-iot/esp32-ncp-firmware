factory_bin: all
	echo $(ESPTOOL_WRITE_FLASH_OPTIONS) $(ESPTOOL_ALL_FLASH_ARGS) | sed -e 's:'$(PWD)/build/'::g' | \
		tail -n 1 > $(BUILD_DIR_BASE)/download.config
	$(PYTHON) $(ESP_AT_PROJECT_PATH)/tools/esp32_at_combine.py \
		--bin_directory $(BUILD_DIR_BASE) \
		--flash_mode $(CONFIG_ESPTOOLPY_FLASHMODE) \
		--flash_size $(CONFIG_ESPTOOLPY_FLASHSIZE) \
		--flash_speed $(CONFIG_ESPTOOLPY_FLASHFREQ)
