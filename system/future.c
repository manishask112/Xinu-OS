#include <xinu.h>
#include <future.h>

future_t* future_alloc(future_mode_t mode, uint size, uint nelems){
	intmask mask;
	mask = disable();
	future_t* new_future = (future_t*)getmem(sizeof(future_t));
	new_future->data = (char *)getmem(sizeof(char) * size * nelems);
	new_future->state = FUTURE_EMPTY;
	new_future->mode = mode;
	new_future->size = size;
	new_future->pid= 0;
	new_future->set_queue = NULL;
	new_future->get_queue = NULL;
	
	new_future->max_elems = nelems;
	new_future->count = 0;
	new_future->head = 0;
	new_future->tail = 0;
	restore(mask);
	return new_future;
}

syscall future_free(future_t* f){
	intmask mask;
	mask = disable();
	if (f!=NULL){
		if(f->get_queue != NULL){
			queue* temp;
			while(f->get_queue != NULL){
				temp = f->get_queue->next;
				freemem((char*)f->get_queue,sizeof(queue));
				f->get_queue = temp;
			}
		}	
		if(f->set_queue != NULL){
			queue* temp;
			while(f->set_queue != NULL){
				temp = f->set_queue->next;
				freemem((char*)f->set_queue,sizeof(queue));
				f->set_queue = temp;
			}
		}
		//freemem((char*)f->set_queue,sizeof(queue));
											
		freemem((char*)f,sizeof(future_t));
		restore(mask);
		return OK;
	}
	restore(mask);
	return SYSERR;
}	
				

syscall future_get(future_t* f, char* data){
	intmask mask;
	mask = disable();
	if (f == NULL){
		restore(mask);
		return SYSERR;
	}	
	if (f->mode == FUTURE_EXCLUSIVE){
		if (f->pid != 0)
		{	
			//restore(mask);
			if (f->get_queue == NULL){ //First process to request for access to future
				f->get_queue = (queue*)getmem(sizeof(queue));
				f->get_queue->pid = getpid();
				f->get_queue->next = NULL;
			}
			else{
				queue* temp = f->get_queue;
				while(temp->next != NULL)
					temp = temp->next;
				temp->next = (queue*)getmem(sizeof(queue));
				temp->next->pid = getpid();
				temp->next->next = NULL;
			}
			suspend(getpid());
			restore(mask);
			return SYSERR; //to allow only one process to access future.
		}
		f->pid = getpid();
		if (f->state != FUTURE_READY) suspend(f->pid);
		memcpy(data,f->data,f->size);
		f->pid = 0;
		f->state = FUTURE_EMPTY;		
	}
	else if(f->mode == FUTURE_SHARED){
		//pid32 pid = getpid();
		if (f->state != FUTURE_READY){
			pid32 pid = getpid();
			if (f->get_queue == NULL){ //First process to request for access to future
				f->get_queue = (queue*)getmem(sizeof(queue));
				f->get_queue->pid = pid;
				f->get_queue->next = NULL;
			}
			else{
				queue* temp = f->get_queue;
				while(temp->next != NULL)
					temp = temp->next;
				temp->next = (queue*)getmem(sizeof(queue));
				temp->next->pid = pid;
				temp->next->next = NULL;
			}
			suspend(pid);
		}
		//kprintf("in get and about to memcpy %d\n",(int)f->data);
		memcpy(data,f->data,f->size);
	}
	else if(f->mode == FUTURE_QUEUE){
		if(f->state != FUTURE_READY){
			//kprintf("data queue is empty, can't consume\n");
			pid32 pid = getpid();
			f->state = FUTURE_WAITING;
			if (f->get_queue == NULL){ //First process to request for access to future
				f->get_queue = (queue*)getmem(sizeof(queue));
				f->get_queue->pid = pid;
				f->get_queue->next = NULL;
				//kprintf("Empty %d\n",pid);
			}
			else{
				queue* temp = f->get_queue;
				//kprintf("Traversing get queue to insert into new position\n");
				while(temp->next != NULL){
					//kprintf("%d\n",temp->pid);
					temp = temp->next;
				}
				temp->next = (queue*)getmem(sizeof(queue));
				temp->next->pid = pid;
				temp->next->next = NULL;
				//kprintf("Waiting %d\n",pid);
			}
			suspend(pid);
		}
		//if(f->state == FUTURE_READY){
			char* headelemptr = f->data + (f->head * f->size);
			memcpy(data,headelemptr,f->size);
			f->head = (f->head + 1) % f->max_elems;
			f->count--;
			//kprintf("Consumed from data queue%d\n",(int)*data);
			if (f->count == 0)
				f->state = FUTURE_EMPTY;
		
	
		if(f->set_queue != NULL){
			struct queue* tmp = f->set_queue;
			f->set_queue = f->set_queue->next;
			resume(tmp->pid);
			//printf("resumed first process in set Q\n");	
		}
	}
	restore(mask);	
	return OK;
}

syscall future_set(future_t* f, char* data){
	intmask mask;
	mask = disable();
	if (f == NULL){
		restore(mask);
		return SYSERR;
	}
	if (f->mode == FUTURE_EXCLUSIVE){
		if (f->state == FUTURE_EMPTY){
			f->state = FUTURE_WAITING;
			memcpy(f->data,data,f->size);
			f->state = FUTURE_READY;
			
			if (f->pid != 0) resume(f->pid); //Resume the only other thread allowed to get value, if it exists.
		}
		else
		{
			queue* temp = f->get_queue;
			while(temp){    //While loop to resume all consumers that cannot consume the future
				resume(temp->pid);
				f->get_queue = f->get_queue->next;
				temp = f->get_queue;
			}
			//restore(mask);
			//return SYSERR;//Future data can be set only once
		}	
	}
	else if(f->mode == FUTURE_SHARED){
		if (f->state == FUTURE_EMPTY){
			f->state = FUTURE_WAITING;
			//kprintf("In set and about to memcpy data %d\n",(int)f->data);
			memcpy(f->data,data,f->size);
			f->state = FUTURE_READY;
			queue* temp = f->get_queue;
			while(temp){	//While loop to start all processes waiting to get future from queue
				resume(temp->pid);
				f->get_queue = f->get_queue->next;
				temp = f->get_queue;
			}
		}
		else
		{
			restore(mask);
			return SYSERR;//Future data can be set only once
		}
	}
	else if(f->mode == FUTURE_QUEUE){
		if(f->count == f->max_elems){
				pid32 pid = getpid();
				//kprintf("data queue is full, can't produce\n");
				if (f->set_queue == NULL){ //First process to request for access to future
					f->set_queue = (queue*)getmem(sizeof(queue));
					f->set_queue->pid = pid;
					f->set_queue->next = NULL;
					//kprintf("Added first element into set queue: %d\n",(int)*data);
				}
				else{
					queue* temp = f->set_queue;
					//kprintf("Traversing set queue to insert into the end\n");
					while(temp->next != NULL){
						//kprintf("%d\n",temp->pid);
						temp = temp->next;
					}
					temp->next = (queue*)getmem(sizeof(queue));
					temp->next->pid = pid;
					temp->next->next = NULL;
					//kprintf("Added %d to the end of set queue\n",(int)*data);
				}
				suspend(pid);
		}
		//if(f->count != f->max_elems){
			char* tailelemptr = f->data + (f->tail * f->size);
			memcpy(tailelemptr,data,f->size);
			f->tail = (f->tail + 1) % f->max_elems;
			f->count++;
			//kprintf("Adding %d into data Q\n",(int)*data);
			f->state = FUTURE_READY;
			
		
		if(f->get_queue != NULL){
			struct queue* tmp = f->get_queue;
			f->get_queue = f->get_queue->next;
			resume(tmp->pid);			
		}
		
			
	}
	restore(mask);
	return OK;
	}
