// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include "settings.h"
#include "types.h"
#include "window.h"

static void
send_signal_to_labwc_pid(int signal)
{
	char *labwc_pid = getenv("LABWC_PID");
	if (!labwc_pid) {
		exit(EXIT_FAILURE);
	}
	int pid = atoi(labwc_pid);
	if (!pid) {
		exit(EXIT_FAILURE);
	}
	kill(pid, signal);
}

static const struct option long_options[] = {
	{"config", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	{0, 0, 0, 0}
};

static const char regions_usage[] =
"Usage: labwc-regions [options...]\n"
"  -c, --config <file>      Specify config file (with path)\n"
"  -h, --help               Show help message and quit\n";

static void
usage(void)
{
	printf("%s", regions_usage);
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct state state = { 0 };
	struct config config = { 0 };
	struct window window = { 0 };

	state.config = &config;
	state.window = &window;
	window.data = &state;

	char *opt_config_file = NULL;
	int c;
	while (1) {
		int index = 0;
		c = getopt_long(argc, argv, "c:h", long_options, &index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'c':
			opt_config_file = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}
	if (optind < argc) {
		usage();
	}

	log_init(LOG_DEBUG);

	window_init(&window);

	if (opt_config_file) {
		strcpy(config.filename, opt_config_file);
	} else {
		snprintf(config.filename, sizeof(config.filename),
			"%s/.config/labwc/rc.xml", getenv("HOME"));
	}

	state.config->regions = settings_init(state.config->filename);

	window_run(&window);

	settings_save(&state);
	settings_finish();
	send_signal_to_labwc_pid(SIGHUP);

	/* 
	 * Finish window after saving settings because window width/height is
	 * required to calculate percentages
	 */
	window_finish(&window);

	return 0;
}
