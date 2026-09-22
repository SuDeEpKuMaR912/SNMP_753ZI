#ifndef INC_BMS_MONITOR_H_
#define INC_BMS_MONITOR_H_

#include "main.h"

void BMS_Process(void);   // call every main-loop iteration, unconditionally
void Read_BMS_Data(void);

#endif /* INC_BMS_MONITOR_H_ */
