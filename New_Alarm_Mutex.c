/*
* New_Alarm_mutex.c
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
#include <limits.h>

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
	long                 status;
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
	int message_type;
} alarm_thread_t;

/*
* Insert alram entry in global alarm_list by MessageType.
*/
void alarm_insert(alarm_t *alarm)
{
	int status;
	alarm_t **last, *next;
	/*
	Place alarm in list by message type
	*/
	last = &alarm_list;
	next = *last;
	while(next != NULL){
		if(next->message_type >= alarm->message_type){
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
	* Wake all alarm threads if it is not busy; that is if
	the thread has no alarm assigned to it, or it has a alarm, but
	sleeps longer
	* which the alarm thread is waiting.
	*/
	status = pthread_cond_broadcast(&alarm_cond);
	if(status != 0)
	err_abort(status, "Broadcast cond");

}

/*
Removes alarm from the global alarm_list after being assigned to a thread.
*/
void alarm_remover(alarm_t *alarm){
	alarm_t *temp_alarm,*temp_alarm_past;
	temp_alarm_past=NULL;
	for(temp_alarm = alarm_list; temp_alarm!= NULL; temp_alarm_past=temp_alarm, temp_alarm = (temp_alarm->link)){
		if(temp_alarm==alarm){
			if(temp_alarm_past==NULL){
				alarm_list=temp_alarm->link;
			}
			else{
				temp_alarm_past->link=temp_alarm->link;
			}
			temp_alarm->link=NULL;

		}
	}
#ifdef DEBUG
	printf("[list: ");
	for(temp_alarm = alarm_list; temp_alarm != NULL; temp_alarm = temp_alarm->link)
	printf("%d(%d)[\"%s\"] ", temp_alarm->time,
	temp_alarm->time/* = time (NULL)*/, temp_alarm->message);
	printf("]\n");
#endif

}

/*
* The alarm thread's start routine.
*/
void *alarm_thread (void *arg)
{
	alarm_t *alarm,*current_alarm,*thread_alarm_list;
	int sleep_time;
	time_t now;
	int status;
	int type_of_thread = *((int *) arg);
	current_alarm=NULL;
	thread_alarm_list=NULL;

	/*
	* Loop forever, processing commands. The alarm thread will
	* be disintegrated when the process exits.
	*/
	pthread_cleanup_push(pthread_mutex_unlock, &alarm_mutex);
	while (1) {
		status = pthread_mutex_lock (&alarm_mutex);
		if (status != 0)
		err_abort (status, "Lock mutex");
		alarm = alarm_list;

		/*
Thread checks to see if alarm in list with same MessageType and not already assigned is available
*/
		if(alarm!=NULL){
			while(alarm->message_type!=type_of_thread || alarm->status!=0){
				if(alarm->link != NULL)
				alarm=alarm->link;
				else{
					alarm=NULL;
					break;
				}

			}
		}

		/*
If thread does not have an alarm after checking the list, it waits until a new alarm is put into the list, and looks at list again
*/
		if (alarm == NULL &&  thread_alarm_list==NULL){
			//printf("Empty list\n");
			status = pthread_cond_wait(&alarm_cond, &alarm_mutex);
			if(status != 0)
			err_abort(status, "Wait on cond");
			status = pthread_mutex_unlock (&alarm_mutex);
			if (status != 0)
			err_abort (status, "Unlock mutex");
			continue;
		}
		/*
Thread found a new alarm in the list, or already has an alarm
*/
		else{
			/*
If thread found new alarm, assign it, and put it in the thread's sub list
*/
			if(alarm!=NULL){
				/*
assign alarm to thread
*/
				alarm->status=pthread_self();
				/*
Remove the thread from the main list
*/
				alarm_remover(alarm);
				printf("Alarm With Message Type(%d)Assigned to Alarm Thread %ld at %d : %c\n",alarm->message_type,(long)pthread_self(),time (NULL),'A');
				alarm->time=time (NULL)+alarm->seconds;
				alarm_t **last, *next;
				/*
	Place alarm in list by time
	*/
				last = &thread_alarm_list;
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
				/*
Assign the current_alarm with the alarm with the shortest time
*/
				current_alarm=thread_alarm_list;
			}
			/*
If the current_alarm is ready to go, print the message. and remove it from the thread sublist
*/
			now=time(NULL);
			if (current_alarm->time <= now){
				status = pthread_mutex_unlock (&alarm_mutex);
				if (status != 0)
				err_abort (status, "Unlock mutex");
				printf ("(%d) %s\n", current_alarm->seconds, current_alarm->message);
				printf("Alarm With Message Type(%d)Printed by Alarm Thread %ld at %d : %c \n",current_alarm->message_type,(long)pthread_self(),time (NULL),'A');
				if(current_alarm->link==NULL)
				thread_alarm_list=NULL;
				else
				thread_alarm_list=current_alarm->link;
				free(current_alarm);
				current_alarm=thread_alarm_list;

			}
			/*
If the current_alarm is not ready to go, go back to the list, and check if new alarm with same message type is available
*/
			else{
				status = pthread_mutex_unlock (&alarm_mutex);
				if (status != 0)
				err_abort (status, "Unlock mutex");
				continue;
			}
		}

	}
	pthread_cleanup_pop(1);
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

	/*
* Parse input line into seconds (%d) and a message
* (%128[^\n]), consisting of up to 128 characters
* separated from the seconds by whitespace.
*/

	if(sscanf(line, "%d %s %128[^\n]", alarm_second, str_msg_type, message) == 3)
	{
		ret_value = 3;
		sscanf(str_msg_type,"%*[^0123456789]%d",msg_type);

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


//Main Function, or Main thread
int main (int argc, char *argv[])
{
	int status;
	char line[256];
	char message[128];
	unsigned int alarm_second;
	alarm_t *alarm, **last, *next;
	int message_type_len;
	unsigned int message_type;
	int cmd_type;
	alarm_thread_t *head_thread, *last_thread, *thread_node;
	head_thread = last_thread = thread_node = NULL;
	pthread_t thread;

	//Loop runs until terminated
	while (1) {
		printf ("Alarm> ");
		if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
		if (strlen (line) <= 1) continue;


		//Get Command Type
		cmd_type = get_cmd_type(line, &message_type, &alarm_second, message);
		switch(cmd_type){
			//If Type B
		case 1:{
				int i = message_type;
				status = pthread_create (&thread, NULL, alarm_thread, &i);
				if (status != 0)
				err_abort (status, "Create alarm thread");

				thread_node = (alarm_thread_t*)calloc(1,sizeof (alarm_thread_t));
				thread_node->thread_id = thread;
				thread_node->message_type = message_type;

				if(head_thread == NULL){
					head_thread = last_thread = thread_node;


				}else
				{
					last_thread->link = thread_node;
					last_thread = thread_node;

				}


				alarm_thread_t *temp;
				printf("New Alarm Thread %ld For Message Type (%d) Created at %d: Type B.\n", thread, message_type, time(NULL));

				break;

				// Type C
			}case 2:{
				terminated_message_type = message_type;
				int contains=0;
				alarm_thread_t *temp_thread,*temp_thread_past;
				/*Remove thread of MessageType(x) from list, and cancel thread
					*/
				temp_thread_past=NULL;
				for(temp_thread= head_thread; temp_thread!=NULL;){
					if((temp_thread->message_type)==terminated_message_type){
						contains=1;
						//printf("MessageType is here\n");
						pthread_cancel(temp_thread->thread_id);
						if(head_thread==temp_thread)
						head_thread=temp_thread->link;
						else
						temp_thread_past->link=temp_thread->link;
						free(temp_thread);
						if(temp_thread_past==NULL){
							temp_thread=head_thread;

						}
						else{
							temp_thread=temp_thread_past->link;
						}


					}
					else{
						temp_thread_past=temp_thread;
						temp_thread = (temp_thread->link);

					}
				}


				/*
						remove the alarms with specified MessageType
						*/

				alarm_t *temp_alarm,*temp_alarm_past;
				temp_alarm_past=NULL;
				for(temp_alarm= alarm_list; temp_alarm!=NULL;){
					//printf("%d %d\n",temp_alarm->message_type,terminated_message_type);
					if((temp_alarm->message_type)==terminated_message_type){
						contains=1;
						//printf("MessageType is here\n");
						if(alarm_list==temp_alarm)
						alarm_list=temp_alarm->link;
						else
						temp_alarm_past->link=temp_alarm->link;
						free(temp_alarm);
						if(temp_alarm_past==NULL){
							temp_alarm=alarm_list;
						}
						else{
							temp_alarm=temp_alarm_past->link;
						}

					}
					else{
						temp_alarm_past=temp_alarm;
						temp_alarm = (temp_alarm->link);
					}
				}

				if (contains){
					printf("All Alarm Thread For Message Type (%d) Terminated And All Messages of Message Type Removed at <%d>:C\n",terminated_message_type,time(NULL) );
				}else{
					printf("MessageType thread not here\n");
				}
				#ifdef DEBUG
				alarm_thread_t *temp;
				for(temp= head_thread; temp!=NULL && head_thread != NULL; temp= (temp ->link))
				printf("Thread: %ld %d\n", temp->thread_id,temp->message_type);

				printf("[list: ");
				for(next = alarm_list; next != NULL; next = next->link)
				printf("%d(%d)[\"%s\"] ", next->time,
				next->time, next->message);
				printf("]\n");
				#endif


				break;


				//Type A
			}case 3:{
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
				alarm->link = NULL;
				strcpy(alarm->message, message);

				/*
				* Insert the new alarm into the list of alarms,
				* sorted by expiration time.
				*/
				alarm_insert(alarm);
				printf("Alarm Request With Message Type (%d) Inserted by Main Thread %ld Into Alarm List at %d: Type A\n", alarm->message_type, pthread_self(), time (NULL));

				status = pthread_mutex_unlock (&alarm_mutex);
				if (status != 0)
				err_abort (status, "Unlock mutex");
				break;

			}case -1:{
				fprintf (stderr, "Bad command\n");
				break;
			}
		}
	}
}
