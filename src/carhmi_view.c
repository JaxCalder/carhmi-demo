#include "dmod.h"

/**
 * @brief carhmi_view - the view of carhmi (assets/car_hmi.html), as a package.
 *
 * The module is its views: install it (dmf-get) where the view should be and
 * carhmi shows it - on the SD card, as the views are too big for a
 * microcontroller's flash. It does nothing when run.
 */
int main(int argc, char* argv[])
{
    Dmod_Printf("carhmi_view: the views of carhmi (carhmi.dmv, its fonts and images)\n");
    return 0;
}
