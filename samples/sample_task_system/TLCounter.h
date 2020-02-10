/*
MIT License

Copyright (c) 2019 Advanced Micro Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//---------------------------------------------------------------------------------------
// Counter that threads can increment or wait on until decremented to 0
//---------------------------------------------------------------------------------------

#pragma once

#include "TLCommon.h"
#ifdef _MSC_VER
#include "Windows.h"
#else
#include <mutex>
#include <condition_variable>
#endif

#define TL_WAKE_ONE 1

namespace AMD
{

#ifdef _MSC_VER
    // Counter with condition variable, allowing threads to sleep until counter decremented to 0.
    // Counter value is signed to allow negative values, for cases with non-deterministic order of increments/decrements.
    // While counter value is <= 0 no thread can stay asleep - decrement to 0 will wake threads and prevent sleeping.
    class TLCounter
    {
        TL_ALIGN(64) CRITICAL_SECTION criticalSection TL_ALIGN_END(64);
        CONDITION_VARIABLE conditionVar;
        volatile int32_t counter;
        volatile int32_t numWaiters;

    public:
        TL_CLASS_NEW_DELETE(TLCounter)

        inline TLCounter()
        {
            InitializeCriticalSectionEx(&criticalSection, 1000, 0);
            InitializeConditionVariable(&conditionVar);
            counter = 0;
            numWaiters = 0;
        }

        inline ~TLCounter()
        {
            DeleteCriticalSection(&criticalSection);
        }

        // Wait/sleep while counter > 0
        inline bool WaitUntilZero()
        {
            bool didWait = false;
            EnterCriticalSection(&criticalSection);
            while (counter > 0)
            {
                numWaiters++;
                SleepConditionVariableCS(&conditionVar, &criticalSection, INFINITE);
                numWaiters--;

                didWait = true;
            }

#if TL_WAKE_ONE
            if (numWaiters > 0)
            {
                WakeConditionVariable(&conditionVar);
            }
#endif

            LeaveCriticalSection(&criticalSection);
            return didWait;
        }

        // If counter > 0, wait until wakeup.
        // Can use to put worker back into spin wait rather than sleep
        inline bool WaitOneWakeup()
        {
            bool didWait = false;
            EnterCriticalSection(&criticalSection);
            if (counter > 0)
            {
                numWaiters++;
                SleepConditionVariableCS(&conditionVar, &criticalSection, INFINITE);
                numWaiters--;

                didWait = true;
            }

#if TL_WAKE_ONE
            if (numWaiters > 0)
            {
                WakeConditionVariable(&conditionVar);
            }
#endif

            LeaveCriticalSection(&criticalSection);
            return didWait;
        }

        // Increment count of active work
        inline int32_t Increment()
        {
            EnterCriticalSection(&criticalSection);
            counter++;
            int32_t lcounter = counter;
            LeaveCriticalSection(&criticalSection);
            return lcounter;
        }

        // Decrement count of active work, and if 0, wake waiters
        inline int32_t Decrement()
        {
            EnterCriticalSection(&criticalSection);
            counter--;
            int32_t lcounter = counter;
            if (lcounter <= 0)
            {
                // Finish using all data before unlocking, in case waiting thread might delete this.

#if TL_WAKE_ONE
                // Wake one waiter
                WakeConditionVariable(&conditionVar);
#else
                WakeAllConditionVariable(&conditionVar);
#endif
            }

            LeaveCriticalSection(&criticalSection);
            return lcounter;
        }

        // Add to count of active work
        inline int32_t Add(int32_t count)
        {
            EnterCriticalSection(&criticalSection);
            counter += count;
            int32_t lcounter = counter;
            LeaveCriticalSection(&criticalSection);
            return lcounter;
        }

        // Subtract from count of active work, and if 0, wake waiters
        inline int32_t Subtract(int32_t count)
        {
            EnterCriticalSection(&criticalSection);
            counter -= count;
            int32_t lcounter = counter;
            if (lcounter <= 0)
            {
                // Finish using all data before unlocking, in case waiting thread might delete this.

#if TL_WAKE_ONE
                // Wake one waiter
                WakeConditionVariable(&conditionVar);
#else
                WakeAllConditionVariable(&conditionVar);
#endif
            }

            LeaveCriticalSection(&criticalSection);
            return lcounter;
        }
    };
#else
    class TLCounter
    {
				std::mutex mutex;
				std::condition_variable condVar;
				int32_t counter = 0;
				int32_t numWaiters = 0;

				using unique_lock = std::unique_lock<std::mutex>;

    public:
        TL_CLASS_NEW_DELETE(TLCounter)

        inline TLCounter()
        {
        }

        inline ~TLCounter()
        {
        }

        // Wait/sleep while counter > 0
        inline bool WaitUntilZero()
        {
            bool didWait = false;
						unique_lock lock(mutex);
            while (counter > 0)
            {
                numWaiters++;
								condVar.wait(lock);
                numWaiters--;

                didWait = true;
            }

#if TL_WAKE_ONE
            if (numWaiters > 0)
            {
								condVar.notify_one();
            }
#endif

            return didWait;
        }

        // If counter > 0, wait until wakeup.
        // Can use to put worker back into spin wait rather than sleep
        inline bool WaitOneWakeup()
        {
            bool didWait = false;
						unique_lock lock(mutex);
            if (counter > 0)
            {
                numWaiters++;
								condVar.wait(lock);
                numWaiters--;

                didWait = true;
            }

#if TL_WAKE_ONE
            if (numWaiters > 0)
            {
								condVar.notify_one();
            }
#endif

            return didWait;
        }

        // Increment count of active work
        inline int32_t Increment()
        {
						unique_lock lock(mutex);
						return ++counter;
        }

        // Decrement count of active work, and if 0, wake waiters
        inline int32_t Decrement()
        {
						unique_lock lock(mutex);
            counter--;
            if (counter <= 0)
            {
                // Finish using all data before unlocking, in case waiting thread might delete this.

#if TL_WAKE_ONE
                // Wake one waiter
								condVar.notify_one();
#else
								condVar.notify_all();
#endif
            }
            return counter;
        }

        // Add to count of active work
        inline int32_t Add(int32_t count)
        {
						unique_lock lock(mutex);
            counter += count;
            return counter;
        }

        // Subtract from count of active work, and if 0, wake waiters
        inline int32_t Subtract(int32_t count)
        {
						unique_lock lock(mutex);
            counter -= count;
            if (counter <= 0)
            {
                // Finish using all data before unlocking, in case waiting thread might delete this.

#if TL_WAKE_ONE
                // Wake one waiter
								condVar.notify_one();
#else
								condVar.notify_all();
#endif
            }

            return counter;
        }
    };

#endif

}
