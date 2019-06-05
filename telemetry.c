#if 1
/*
 *  ======== telemetry.c ========
 */

#include "Board.h"

#include <ti/sysbios/BIOS.h>

/* Application header files */
#include "radioTask.h"

/*
 *  ======== main ========
 */
int main()
{
    /* Call driver init functions */
    Board_init();

    radioTask_init();

    /* Start BIOS */
    BIOS_start();

    return(0);
}
#endif
