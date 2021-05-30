// scheduler.cc
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would
//	end up calling FindNextToRun(), and that would put us in an
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{
    //readyList = new List<Thread *>;
    L1 = new List<Thread *>;
    L2 = new List<Thread *>;
    L3 = new List<Thread *>;
    toBeDestroyed = NULL;
}

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{
    //delete readyList;
    delete L1;
    delete L2;
    delete L3;
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void Scheduler::ReadyToRun(Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
    //cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    
    // Check which level to place
    int thread_priority = thread->GetPriority();

    if(thread_priority >= 100 && thread_priority <= 149){ // L1
        InsertToQueue(L1, 1, thread);
    }
    else if(thread_priority >= 50 && thread_priority <= 99){ // L2
        InsertToQueue(L2, 2, thread);
    }
    else if(thread_priority >= 0 && thread_priority <= 49){ // L3
        InsertToQueue(L3, 3, thread);
    }
    //readyList->Append(thread);
    thread->UpdateAgeBaseline();
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    
    if(!L1->IsEmpty()){
        ListIterator<Thread*> *iter = new ListIterator<Thread*>(L1); // In list.h has tutorial

        Thread *lowest = L1->Front(); // take the front of queue to be the lowest burst time Thread first
        double min_appro_burst = lowest->GetPredict(); // GetPredict() can get the approximate burst time

        for (; !iter->IsDone(); iter->Next()) {
            double appro_burst = iter->Item()->GetPredict(); // iter->Item() will get the thread in queue
            if(appro_burst < min_appro_burst){
                min_appro_burst = appro_burst;
                lowest = iter->Item();
            }
        }
        lowest->AddTicksInQueue();
        return Removethread(L1, 1, lowest);
    }
    else if(!L2->IsEmpty()){
        ListIterator<Thread*> *iter = new ListIterator<Thread*> (L2);

        Thread* highest = L2->Front();
        int high_p = highest->GetPriority();
        for (; !iter->IsDone(); iter->Next()) {
            Thread* tmp_thread = iter->Item();
            int p = tmp_thread->GetPriority();
            if(p > high_p){
                high_p = p;
                highest = iter->Item();
            }
        }
        highest->AddTicksInQueue();
        return Removethread(L2, 2, highest);
    }
    else if(!L3->IsEmpty()){
        L3->Front()->AddTicksInQueue();
        return Removethread(L3, 3, L3->Front());
    }
    else{ // L1, L2, L3 is empty
        return NULL;
    }
    // if (readyList->IsEmpty())
    // {
    //     return NULL;
    // }
    // else
    // {
    //     return readyList->RemoveFront();
    // }
}

Thread* Scheduler::Removethread(List<Thread *> *Readyqueue, int level, Thread* nextThread){
    Readyqueue->Remove(nextThread);
    DEBUG(dbgKYL,"[B] Tick ["<< kernel->stats->totalTicks <<"]: Thread [" <<nextThread->getID() << "] is removed from queue L["<<level <<"]");
    return nextThread;
}

void Scheduler::InsertToQueue(List<Thread *> *Readyqueue, int level, Thread* inThread){
    DEBUG(dbgKYL,"[A] Tick ["<< kernel->stats->totalTicks <<"]: Thread [" <<inThread->getID() << "] is inserted into queue L["<<level <<"]");
    Readyqueue->Append(inThread);
}

void Scheduler::DoAgeThreeQueue(){ // Age the thread in different level queue
    AgeQueue(L3, 3);
    AgeQueue(L2, 2);
    AgeQueue(L1, 1);
}

void Scheduler::AgeQueue(List<Thread *> *Readyqueue, int level){
     ListIterator<Thread*> *iter = new ListIterator<Thread*> (Readyqueue);
     Thread* curThread;
     // Age every thread in queue
     for (; !iter->IsDone(); iter->Next()){
        curThread = iter->Item();

        curThread->AddTicksInQueue();
        curThread->UpdateAgeBaseline();
        if(curThread->HandleAgingOld()){ // check if thread in queue over 1500 ticks
            curThread->SetPriority(10); // add its priority to avoid starvation

            if(level == 3 && curThread->GetPriority() > 49){ // if a L3 thread need to upgrade
                Removethread(L3, 3, curThread);
                InsertToQueue(L2, 2, curThread);
            }
            else if(level == 2 && curThread->GetPriority() > 99){ // if a L2 thread need to upgrade
                Removethread(L2, 2, curThread);
                InsertToQueue(L1, 1, curThread);
            }
        }
     }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void Scheduler::Run(Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing)
    { // mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL)
    {                               // if this thread is a user program,
        oldThread->SaveUserState(); // save the user's CPU registers
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow(); // check if the old thread
                                // had an undetected stack overflow

    kernel->currentThread = nextThread; // switch to the next thread
    nextThread->setStatus(RUNNING);     // nextThread is now running

    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    DEBUG(dbgKYL, "[E] Tick ["<<kernel->stats->totalTicks << "]: Thread ["<<nextThread->getID() << "] is now selected for execution, thread ["<<oldThread->getID() << "] is replaced, and it has executed ["<<oldThread->GetExecTime() << "] ticks");
    // This is a machine-dependent assembly language routine defined
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".
    nextThread->setBurstStart();
    SWITCH(oldThread, nextThread);
    oldThread->setBurstStart();
    // we're back, running oldThread

    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed(); // check if thread we were running
                          // before this one has finished
                          // and needs to be cleaned up

    if (oldThread->space != NULL)
    {                                  // if there is an address space
        oldThread->RestoreUserState(); // to restore, do it.
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL)
    {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//--------------------------------------------------------