#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/framework/include/af.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/ezsp-host/ezsp-host-ui.h>

bool emberAfMainStartCallback(int* returnCode,
                              int argc,
                              char** argv)
{
    if (!ezspProcessCommandOptions(argc, argv)) {
        *returnCode = EMBER_ERR_FATAL;
        return true;
    }

    return false;
}

int main(int argc, char* argv[]) {
    emberAfMainInit();
    return emberAfMain(argc, argv);
}
