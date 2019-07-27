#pragma once

#include "interfaces.h"


int if_dbt03_status(int x);
int if_dbt03_read(int x);
int if_dbt03_write(int x);

extern io_type_t if_dbt03;
