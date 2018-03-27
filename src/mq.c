#include <stdlib.h>
#include <argp.h>
#include <mqueue.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <poll.h>

#define PROG_NAME "mq"
const char *argp_program_version = PROG_NAME " 1.0";

static char doc[] = 
	"A command line tool to use Posix Message Queues from the shell"
	"\vCommands:\n"
	"  create    Create a POSIX message queue\n"
	"  info      Print information about an existing message queue\n"
	"  unlink    Delete a message queue\n"
	"  send      Send a message to a message queue\n"
	"  recv      Receive and print a message from a message queue\n"
	"\n"
	"Examples:\n"
	"  mq create /myqueue\n"
	"  mq send /myqueue \"hello\" -n\n"
	"  mq info /myqueue\n"
	"  mq recv /myqueue\n"
	"  mq unlink /myqueue\n"
					 ;

static char args_doc[] =
	"create QNAME\n"
	"info QNAME\n"
	"unlink QNAME\n"
	"send QNAME MESSAGE\n"
	"recv QNAME"
	;

#define LOG_ERR(...) \
		do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)

#define LOG_VERBOSE(_args, ...) \
		do { if (_args->verbose) LOG_ERR(__VA_ARGS__); } while (0)

#define LOG_DATA(_args, ...) \
		do { if (_args->timestamp) printf("%s ", get_timestamp()); \
		     printf(__VA_ARGS__); printf("\n"); } while (0)

static char *get_timestamp()
{
	static char buffer[256];
	struct tm date;
	struct timeval tv;
	gettimeofday(&tv, 0);
	localtime_r(&tv.tv_sec, &date);
	int milliseconds = tv.tv_usec / 1000;
	snprintf(buffer, sizeof(buffer), "%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%03d",
			date.tm_year + 1900,
			date.tm_mon + 1,
			date.tm_mday,
			date.tm_hour,
			date.tm_min,
			date.tm_sec,
			milliseconds);
	return buffer;
}

void usage(const struct argp *argp)
{
	argp_help(argp, stderr, ARGP_HELP_STD_HELP, PROG_NAME);
}

static struct argp_option options[] = {
	{ 0, 0, 0, 0, "Options:" },
	{ "verbose", 'v', 0, 0, "Produce verbose output" },
	{ "timestamp", 't', 0, 0, "Print a timestamp before lines of data" },
	{ 0, 0, 0, 0, "Options for create:" },
	{ "msgsize", 's', "SIZE", 0, "Message size in bytes" },
	{ "maxmsg", 'm', "NUMBER", 0, "Maximum number of messages in queue" },
	{ 0, 0, 0, 0, "Options for recv:" },
	{ "follow", 'f', 0, 0, "Print messages as they are received" },
	{ 0, 0, 0, 0, "Options for send:" },
	{ "priority", 'p', "PRIO", 0, "Use priority PRIO, PRIO >= 0" },
	{ 0, 0, 0, 0, "Options for send, recv:" },
	{ "non-blocking", 'n', 0, 0, "Do not block (send, recv)" },
	{ 0 }
};

struct arguments
{
	int verbose;
	char *command;
	char *qname; /* name of the mq (should start with '/') */

	/* for command 'create' */
	int maxmsg; /* max number of message */
	int msgsize; /* size of a message */


	int timestamp;
	/* for command 'recv' */
	int blocking;
	int follow;

	/* for command 'send' */
	char *message;
	int priority;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *args = state->input;

	switch (key) {
	case 'v': args->verbose = 1; break;
	case 'n': args->blocking = 0; break;
	case 't': args->timestamp = 1; break;
	case 'f': args->follow = 1; break;
	case 's': args->msgsize = atoi(arg); break;
	case 'm': args->maxmsg = atoi(arg); break;
	case 'p': args->priority = atoi(arg); break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage(state);

	case ARGP_KEY_ARG:
		if (!args->command) args->command = arg;
		else if (!args->qname) args->qname = arg;
		else if (0 == strcmp(args->command, "send") && !args->message) args->message = arg;
		else {
			/* too many arguments */
			argp_usage(state);
		}

		break;

	case ARGP_KEY_END:
		if (!args->command) argp_usage(state);
		if (!args->qname) argp_usage(state);
		if (0 == strcmp(args->command, "send") && !args->message) argp_usage(state);

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static int cmd_create(const struct arguments *args)
{
	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = args->maxmsg;
	attr.mq_msgsize = args->msgsize;
	attr.mq_curmsgs = 0;

	mode_t mode = 0644;

	LOG_VERBOSE(args, "Opening mq %s (O_CREAT, O_RDWR, O_EXCL, %o)", args->qname, mode);

	mqd_t queue = mq_open(args->qname, O_CREAT|O_RDWR|O_EXCL, mode, &attr);

	if (-1 == queue) {
		LOG_ERR("mq_open error: %s", strerror(errno));
		return 1;
	}
	return 0;
}

static int cmd_info(const struct arguments *args)
{
	struct mq_attr attr;
	int ret;

	LOG_VERBOSE(args, "Opening mq %s (O_RDONLY)", args->qname);
	mqd_t queue = mq_open(args->qname, O_RDONLY);
	if (-1 == queue) {
		LOG_ERR("mq_open error: %s", strerror(errno));
		return 1;
	}

	ret = mq_getattr(queue, &attr);
	if (0 != ret) {
		LOG_ERR("mq_getattr error: %s", strerror(errno));
		ret = 1;
	} else {
		LOG_DATA(args, "%s: maxmsg=%ld, msgsize=%ld, curmsgs=%ld",
				 args->qname, attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);
	}

	mq_close(queue);
	return ret;
}

static int cmd_unlink(const struct arguments *args)
{
	LOG_VERBOSE(args, "Deleting mq %s", args->qname);
	int ret = mq_unlink(args->qname);

	if (0 != ret) {
		LOG_ERR("mq_unlink error: %s", strerror(errno));
		return 1;
	}
	return 0;
}

static int cmd_send(const struct arguments *args)
{
	int oflag = O_WRONLY;
	if (!args->blocking) oflag |= O_NONBLOCK;

	LOG_VERBOSE(args, "Opening mq %s (O_WRONLY%s)", args->qname, (oflag & O_NONBLOCK)?", O_NONBLOCK":"");
	mqd_t queue = mq_open(args->qname, oflag);
	if (-1 == queue) {
		LOG_ERR("mq_open error: %s", strerror(errno));
		return 1;
	}

	if (args->verbose) LOG_DATA(args, "> %s", args->message);

	/* Send and keep the null terminating char */
	int ret = mq_send(queue, args->message, strlen(args->message)+1, args->priority);
	if (0 != ret) {
		LOG_ERR("mq_send error: %s", strerror(errno));
		ret = 1;
	}

	mq_close(queue);
	return ret;
}

static mqd_t mqu_open_ro(const struct arguments *args)
{
	mqd_t queue;
	int oflag = O_RDONLY;
	if (!args->blocking) oflag |= O_NONBLOCK;

	LOG_VERBOSE(args, "Opening mq %s (O_RDONLY%s)",
			    args->qname, (oflag & O_NONBLOCK)?", O_NONBLOCK":"");

	queue = mq_open(args->qname, oflag);
	if (-1 == queue) LOG_ERR("mq_open error: %s", strerror(errno));

	return queue;
}

static int cmd_recv(const struct arguments *args)
{
	mqd_t queue;
	int ret;
	uint8_t *buffer;
	struct mq_attr attr;

	queue = mqu_open_ro(args);
	if (-1 == queue) return 1;

	// retrieve the message size
	ret = mq_getattr(queue, &attr);
	if (0 != ret) {
		mq_close(queue);
		return 1;
	}

	buffer = malloc(attr.mq_msgsize);

	ret = mq_receive(queue, (void*)buffer, attr.mq_msgsize, NULL);
	if (ret >= 0) {
		/* got a message */
		LOG_DATA(args, "%s", buffer);
		ret = 0;
	}

	mq_close(queue);
	return ret;
}

static int cmd_recv_follow(const struct arguments *args)
{
	mqd_t queue;
	int ret;
	uint8_t *buffer;
	struct mq_attr attr;
	struct pollfd ufds[1];

	queue = mqu_open_ro(args);
	if (-1 == queue) return 1;

	// retrieve the message size
	ret = mq_getattr(queue, &attr);
	if (0 != ret) {
		mq_close(queue);
		return 1;
	}

	buffer = malloc(attr.mq_msgsize);

	ufds[0].fd = queue;
	ufds[0].events = POLLIN;

	while (1) {
		int rv = poll(ufds, 1, -1); // no timeout
		if (rv == -1) {
			LOG_ERR("poll error: %s", strerror(errno));
		} else if (1 == rv) {
			if (ufds[0].revents & POLLIN) {
				// receive the message
				ret = mq_receive(queue, (void*)buffer, attr.mq_msgsize, NULL);
				if (ret >= 0) {
					/* got a message */
					LOG_DATA(args, "%s", buffer);
				} else {
					LOG_ERR("mq_receive error: %s", strerror(errno));
					break;
				}

			} else {
				LOG_ERR("poll revents != POLLIN (%x)", ufds[0].revents);
				break;
			}
		} else {
			LOG_ERR("poll error(2): rv=%d", rv);
		}
	}
	mq_close(queue);
	return 1;

}

int main(int argc, char **argv)
{
	struct arguments args;
	struct argp argp = { options, parse_opt, args_doc, doc };

	/* Default values */
	args.verbose = 0;
	args.command = NULL;
	args.qname = NULL;
	args.maxmsg = 10;
	args.msgsize = 1024;
	args.timestamp = 0;
	args.blocking = 1;
	args.follow = 0;
	args.message = NULL;
	args.priority = 0;

	argp_parse(&argp, argc, argv, 0, 0, &args);

	if (0 == strcmp(args.command, "create")) return cmd_create(&args);
	else if (0 == strcmp(args.command, "info")) return cmd_info(&args);
	else if (0 == strcmp(args.command, "unlink")) return cmd_unlink(&args);
	else if (0 == strcmp(args.command, "send")) return cmd_send(&args);
	else if (0 == strcmp(args.command, "recv")) {
	   if (args.follow) return cmd_recv_follow(&args);
	   else return cmd_recv(&args);
	}
	else usage(&argp);
}