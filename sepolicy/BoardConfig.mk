# The component owns its app domain and transport rules. Consumers only include
# this file; they must not grant the endpoint to generic platform/priv-app domains.
SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += packages/services/FloralDevice/sepolicy/system_ext/public
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += packages/services/FloralDevice/sepolicy/system_ext/private
BOARD_VENDOR_SEPOLICY_DIRS += packages/services/FloralDevice/sepolicy/vendor
