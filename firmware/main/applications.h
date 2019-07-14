#pragma once

typedef int (*btx_iofunc_t) (int ch);

int application(btx_iofunc_t in, btx_iofunc_t out, btx_iofunc_t status);

