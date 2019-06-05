/*
 * radioTask.h
 *
 *  Created on: Apr 12, 2019
 *      Author: HaiHui
 */

#ifndef RADIOTASK_H_
#define RADIOTASK_H_
#include <xdc/std.h>

void radioTask_init(void);

/* radio task function: control tx/rx */
void radioTaskFnx(UArg arg0, UArg arg1);


#endif /* RADIOTASK_H_ */
