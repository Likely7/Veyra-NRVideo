// SPDX-License-Identifier: GPL-3.0-or-later
#include <munit.h>

extern MunitTest tests_feedback[];

int main(int argc, char **argv)
{
    MunitSuite suite = { "/feedback", tests_feedback, NULL, 1, MUNIT_SUITE_OPTION_NONE };
    return munit_suite_main(&suite, NULL, argc, argv);
}
