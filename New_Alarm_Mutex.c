/*
 * alarm_mutex.c
 *
 * This is an enhancement to the alarm_thread.c program, which
 * created an "alarm thread" for each alarm command. This new
 * version uses a single alarm thread, which reads the next
 * entry in a list. The main thread places new requests onto the
 * list, in order of absolute expiration time. The list is
 * protected by a mutex, and the alarm thread sleeps for at
 * least 1 second, each iteration, to ensure that the main
 * thread can lock the mutex to add new work to the list.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <regex.h>

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    int                 message_type;
    char                message[64];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;
unsigned int terminated_message_type = 0;

typedef struct alarm_thread_tag {
	struct alarm_thread_tag *link;
	pthread_t thread_id;
	int message_type;
} alarm_thread_t;

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    int sleep_time;
    time_t now;
    int status;
    unsigned int message_type = *((unsigned int *) arg);
    pthread_detach(pthread_self()); 
    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits.
     */
    while (message_type != terminated_message_type) {
	//printf("child thread: message type: %d\n", message_type);
        status = pthread_mutex_lock (&alarm_mutex);
        if (status != 0)
            err_abort (status, "Lock mutex");
        alarm = alarm_list;

        /*
         * If the alarm list is empty, wait for one second. This
         * allows the main thread to run, and read another
         * command. If the list is not empty, remove the first
         * item. Compute the number of seconds to wait -- if the
         * result is less than 0 (the time has passed), then set
         * the sleep_time to 0.
         */
        sleep_time = 1;
        while(alarm != NULL){
            alarm_list = alarm->link;
            now = time (NULL);
            if (alarm->time <= now)
                sleep_time = 0;
            else
                sleep_time = alarm->time - now;
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                sleep_time, alarm->message);
#endif
            }

        /*
         * Unlock the mutex before waiting, so that the main
         * thread can lock it to insert a new alarm request. If
         * the sleep_time is 0, then call sched_yield, giving
         * the main thread a chance to run if it has been
         * readied by user input, without delaying the message
         * if there's no input.
         */
        status = pthread_mutex_unlock (&alarm_mutex);
        if (status != 0)
            err_abort (status, "Unlock mutex");
        if (sleep_time > 0)
            sleep (sleep_time);
        else
            sched_yield ();

        /*
         * If a timer expired, print the message and free the
         * structure.
         */
        if (alarm != NULL) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
    pthread_exit(0);
}

/*
 * Insert alram entry on list, in order.
 */
void alarm_insert(alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL;
     *
     * This routine requires that the caller have locked the alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while(next != NULL){
	if(next->time >= alarm->time){
	    alarm->link = next;
	    *last = alarm;
	    break;
	}
	last = &next->link;
	next = next->link;
    }
    /* If we reached the end of the list, insert the new alarm
     * there. ("next" is NULL, and "last" points to the link
     * field of the last item, or to the list header.)
     */
    if(next == NULL){
	*last = alarm;
	alarm->link = NULL;
    }
#ifdef DEBUG
    printf("[list: ");
    for(next = alarm_list; next != NULL; next = next->link)
        printf("%d(%d)[\"%s\"] ", next->time,
	    next->time = time (NULL), next->message);
    printf("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if(current_alarm == 0 || alarm->time < current_alarm){
	current_alarm = alarm->time;
	status = pthread_cond_signal(&alarm_cond);
	if(status != 0)
            err_abort(status, "Signal cond");
    }
}

/**
 Get command type.
 
 \param line information that user input.
 \param msg_type Output message type.
 \param alarm_second If the command is message command, after the alarm_second, 
                     message will be displayed.
 \param message If the command is message command. message contains the message to be
                displayed.
 \return 1 means create thread command, 2 means terminate command, 3 means message command,
         -1 means bad command.
*/
int get_cmd_type(char* line, unsigned int* msg_type, unsigned int* alarm_second, char* message)
{
	char cmd[20];
	char str_msg_type[20];
	int ret_value;
    
	if(sscanf(line, "%d %s %64[^\n]", alarm_second, str_msg_type, message) == 3)
	{   
		ret_value = 3;
        
	}else if(sscanf(line, "%s %s[^\n]",cmd, str_msg_type) == 2)
	{
        if(sscanf(str_msg_type, "%*[^0123456789]%d", msg_type) == 1 && 
           strncmp(str_msg_type,"MessageType(",strlen("MessageType(") - 1 ) == 0){
           if(*msg_type < 1){
               fprintf (stderr, "Message type must be the positive integer.\n");
               ret_value = -1;
           }else if(strcmp(cmd,"Create_Thread:")==0){
			   ret_value = 1;
		   }else if(strcmp(cmd,"Terminate_Thread:") == 0){
			   ret_value = 2;
		   }else
		   {
			   ret_value = -1;
		   }
		}else
		{
			ret_value = -1;
		}
	}else
	{
        fprintf (stderr, "The number of parameters is not correct.\n");
		ret_value = -1;
	}
	
	return ret_value;
}

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    char message[64];
    unsigned int alarm_second;
    alarm_t *alarm, **last, *next;
    pthread_t thread;
    int message_type_len;
    unsigned int message_type;
	int cmd_type;
    alarm_thread_t *head_thread, *last_thread, *thread_node;
    
    head_thread = last_thread = thread_node = NULL;

    while (1) {
        printf ("alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
		cmd_type = get_cmd_type(line, &message_type, &alarm_second, message);
		switch(cmd_type){
			case 1:
				status = pthread_create (&thread, NULL, alarm_thread, &message_type);
				if (status != 0)
				   err_abort (status, "Create alarm thread");
				thread_node = (alarm_thread_t*)malloc(sizeof (alarm_thread_t));
				thread_node->thread_id = thread;
				thread_node->message_type = message_type;
				if(last_thread == NULL){
					head_thread = last_thread = thread_node;
				}else
				{
					last_thread->link = thread_node;
					last_thread = thread_node;
				}
				printf("New Alarm Thread %d For Message Type (%d) Created at %d: <alarm_request>.\n", thread, message_type, time(NULL));
				break;
			case 2:
                terminated_message_type = message_type;
				break;
			case 3:
                alarm = (alarm_t*)malloc (sizeof (alarm_t));
                if (alarm == NULL)
                    errno_abort ("Allocate alarm");
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
                alarm->time = time (NULL) + alarm->seconds;
                printf("hello");

                /*
                 * Insert the new alarm into the list of alarms,
                 * sorted by expiration time.
                 */
                last = &alarm_list;
                next = *last;
                while (next != NULL) {
                    if (next->time >= alarm->time) {
                        alarm->link = next;
                        *last = alarm;
                        break;
                    }
                    last = &next->link;
                    next = next->link;
                }
                /*
                 * If we reached the end of the list, insert the new
                 * alarm there. ("next" is NULL, and "last" points
                 * to the link field of the last item, or to the
                 * list header).
                 */
                if (next == NULL) {
                    *last = alarm;
                    alarm->link = NULL;
                }
    #ifdef DEBUG
                printf ("[list: ");
                for (next = alarm_list; next != NULL; next = next->link)
                    printf ("%d(%d)[\"%s\"] ", next->time,
                        next->time - time (NULL), next->message);
                printf ("]\n");
    #endif
                status = pthread_mutex_unlock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
				break;
			case -1:
                fprintf (stderr, "Bad command\n");
				break;
		}
    }
}
