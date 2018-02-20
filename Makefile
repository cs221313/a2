a2: New_Alarm_Mutex.o
	cc -lpthread -o a2 New_Alarm_Mutex.o

New_Alarm_Mutex.o: New_Alarm_Mutex.c
	cc -c -g New_Alarm_Mutex.c -D_POSIX_PTHREAD_SEMANTICS 
