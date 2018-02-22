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
    int                 status;
    char                message[128];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;
unsigned int terminated_message_type = 0;

typedef struct alarm_thread_tag {
	struct alarm_thread_tag *link;
	pthread_t thread_id;
} alarm_thread_t;

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
	    next->time/* = time (NULL)*/, next->message);
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

void alarm_insert_to_local(alarm_t **alarm_list_in_thread, alarm_t *alarm)
{
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL;
     *
     * This routine requires that the caller have locked the alarm_mutex!
     */
    last = alarm_list_in_thread;
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
    printf("[list in thread: ");
    for(next = *alarm_list_in_thread; next != NULL; next = next->link)
        printf("%d(%d)[\"%s\"] ", next->time,
	    next->time/* = time (NULL)*/, next->message);
    printf("]\n");
#endif
}

/**
 * Get alarm from alarm list first. Then put it in to the alarm list in thread.
 * \param message_type message type of request message.
 * \parma local_list alarm list in thread.
 */
alarm_t* get_alarm(unsigned int message_type, alarm_t** local_list)
{
    /* Get alarm from local list */
    alarm_t* alarm;
    alarm_t* prev_alarm;
    
    /* Get alarm from alarm list */
    alarm = alarm_list;  /* take the first alarm node out of alarm list */
    prev_alarm = alarm_list;
    while(alarm != NULL && alarm->message_type != message_type){
	prev_alarm = alarm;
        alarm = alarm->link;
    }
    if(alarm != NULL && alarm == alarm_list){   /* alarm is the first node in the alarm list */
	alarm_list = alarm->link;
    }else if(alarm != NULL)
    {
        prev_alarm->link = alarm->link; /* remove alarm from alarm_list */
    }

    if(*local_list != NULL){
	if(alarm != NULL){
            alarm_insert_to_local(local_list, alarm);
	}
        alarm = *local_list;
        *local_list = alarm->link;
    }
    return alarm;
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    alarm_t *alarm_list_in_thread = NULL;
    struct timespec cond_time;
    time_t now;
    int status, expired;  //expired: check pthread_cond_timedwait is timeout or not.
    unsigned int message_type = *(unsigned int*)arg;

#ifdef DEBUG
    printf("message_type = %d\n", message_type);
#endif

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");
    while(1){
       /*
        * If the alarm list is empty, wait until an alarm is
        * added. Setting current_alarm to 0 informs the insert
        * routine that the thread is not busy.
        */
        current_alarm = 0;
        while((alarm = get_alarm(message_type, &alarm_list_in_thread)) == NULL && alarm_list == NULL){
#ifdef DEBUG
            printf("[list in thread after get_alarm: ");
	    alarm_t* next;
            for(next = alarm_list_in_thread; next != NULL; next = next->link)
                printf("%d(%d)[\"%s\"] ", next->time,
	        next->time/* = time (NULL)*/, next->message);
    printf("]\n");
#endif
            status = pthread_cond_wait(&alarm_cond, &alarm_mutex);
            if(status != 0)
                err_abort(status, "Wait on cond");
        }
        now = time(NULL);
        expired = 0;
	printf("Alarm Request With Message Type (%d) Assigned to Alarm Thread %d at %d: Type A\n", message_type, pthread_self(), time(NULL));
#ifdef DEBUG
        printf("alarm time in alarm thread: %d, now: %d\n", alarm->time, now);
#endif
        if(alarm->time > now){
#ifdef DEBUG
            printf("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                    alarm->time - time(NULL), alarm->message);
#endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->time;
            while(current_alarm == alarm->time){
                status = pthread_cond_timedwait(
                    &alarm_cond, &alarm_mutex, &cond_time);
                if(status == ETIMEDOUT){
                    expired = 1;
                    break;
                }
                if(status != 0)
                    err_abort(status, "Cond timedwait");
            }
            if(!expired)
                alarm_insert_to_local(&alarm_list_in_thread, alarm); /* if condition wait is not timeout(this is because when the request is still waiting, the new request is in the alarm list, program has to check that new request time is less than to current one or not), put alarm back to alarm_list. because the alarm has assigned to the thread, so we have to put it in local. */
        }else
            expired = 1;
        if(expired){
            printf("Alarm With Message Type(%d) Printed by Alarm Thread %d at %d: Type B Message: %s\n",
			    alarm->message_type, pthread_self(), time(NULL), alarm->message);
            free(alarm);
        }
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
    
	if(sscanf(line, "%d %s %128[^\n]", alarm_second, str_msg_type, message) == 3)
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
    char line[156];
    char message[128];
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
         * (%128[^\n]), consisting of up to 128 characters
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
		if(last_thread == NULL){
                    head_thread = last_thread = thread_node;
		}else
		{
		    last_thread->link = thread_node;
		    last_thread = thread_node;
		}
		printf("New Alarm Thread %d For Message Type (%d) Created at %d: Type B.\n", 
				thread, message_type, time(NULL));
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
                alarm->seconds = alarm_second;
                alarm->time = time (NULL) + alarm->seconds;
#ifdef DEBUG
                printf("alarm time: %d\n", alarm->time);
#endif
                alarm->message_type = message_type;
                alarm->status = 0;
                strcpy(alarm->message, message);

                /*
                 * Insert the new alarm into the list of alarms,
                 * sorted by expiration time.
                 */
                alarm_insert(alarm);
                printf("Alarm Request With Message Type (%d) Inserted by Main Thread %d Into Alarm List at %d: Type A\n", alarm->message_type, pthread_self(), time (NULL));

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
