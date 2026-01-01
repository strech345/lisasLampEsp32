#ifndef STORE_H
#define STORE_H

#include "types.h" // For FullConfig definition

/**
 * @brief Saves the FullConfig object to EEPROM using Preferences.
 * @param config The FullConfig object to save.
 * @return True if save was successful, false otherwise.
 */
bool saveFullConfig(const FullConfig& config, bool debounce = false);
void checkToSave();

/**
 * @brief Loads the FullConfig object from EEPROM using Preferences.
 * @param config The FullConfig object to load data into.
 * @return True if load was successful, false otherwise.
 */
bool loadFullConfig(FullConfig& config);

/**
 * @brief Returns a FullConfig object initialized with default values.
 * @return A FullConfig object with default settings.
 */
FullConfig getDefaultFullConfig();

/**
 * @brief Deletes the FullConfig object from EEPROM using Preferences.
 * @return True if deletion was successful, false otherwise.
 */
/* bool deleteFullConfig(); */

/**
 * @brief Resets the FullConfig object in EEPROM to default values.
 * @return True if reset was successful, false otherwise.
 */
bool resetFullConfig();

/**
 * @brief Checks if the FullConfig namespace exists and contains data. If not,
 * it calls resetFullConfig.
 * @return True if config exists or was successfully reset, false otherwise.
 */
bool ensureConfigExistsAndResetIfNot();

bool saveSystemSettings(const SystemSettings& settings);
bool loadSystemSettings(SystemSettings& settings);

/**
 * @brief Returns a SystemSettings object initialized with default values.
 * @return A SystemSettings object with default settings.
 */
SystemSettings getDefaultSystemSettings();

/**
 * @brief Resets the SystemSettings object in EEPROM to default values.
 * @return True if reset was successful, false otherwise.
 */
bool resetSystemSettings();

/**
 * @brief Checks if the SystemSettings exists and contains data. If not, it
 * calls resetSystemSettings.
 * @return True if settings exist or were successfully reset, false otherwise.
 */
bool ensureSystemSettingsExistsAndResetIfNot();

/**
 * @brief Initializes the FullConfig object in EEPROM if it doesn't exist.
 * @param force If true, forces re-initialization even if config exists.
 * @return True if initialization was successful, false otherwise.
 */
/* bool initFullConfig(bool force); */

#endif // STORE_H
