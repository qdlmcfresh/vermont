/*
 * Thread-safe (concurrent) queue implementation
 * The implementation of the BaseQueue makes it thread-safe
 *
 * Make sure that size of T is 4 bytes on 32 bit operating systems
 * and 8 bytes on 64 bit operating systems accordingly when using
 * QueueType LOCKFREE_MULTI
 *
 * Author: Michael Drueing <michael@drueing.de>
 *
 */

#ifndef CONCURRENT_QUEUE_H
#define CONCURRENT_QUEUE_H

#include <string>
#include "BaseQueue.h"
#include "STLQueue.h"
#include "LockfreeSingleQueue.h"
#include "LockfreeMultiQueue.h"
#include "TimeoutSemaphore.h"
#include "msg.h"

//for other ConcurrentQueues
#include "osdep/linux/sysinfo.h"
#include <pthread.h>
#include <time.h>

template<class T>
class BaseConcurrentQueue
{
	public:
		BaseConcurrentQueue() {}
		virtual ~BaseConcurrentQueue() {}
		virtual void push(T ) = 0;
		virtual bool pop(T* ) = 0;
		virtual bool popAbs(const struct timespec& , T* ) = 0;
		virtual void restart() {}
		//TODO
		virtual void reset() { (*(this->queueImp2))->reset(); }

		BaseQueue<T>* queueImp;
		BaseQueue<T>** queueImp2;
};

/**
 * types of queues
 */
enum QUEUETYPES {STL, SINGLE_CACHEOPT, SINGLE_BASIC, SINGLE_CACHEOPT_LOCAL, MULTI};


template<class T>
class ConcurrentQueue : public BaseConcurrentQueue<T>
{
	public:
		/**
		 * default queue size
		 */
		static const int DEFAULT_QUEUE_SIZE = 1000;

		ConcurrentQueue(int qType = STL, int maxEntries = DEFAULT_QUEUE_SIZE):
			pushedCount(0), poppedCount(0), count(0), popSemaphore(), pushSemaphore(maxEntries)
		{
			this->maxEntries = maxEntries;

			switch(qType){
				case STL:
					this->queueImp = new STLQueue<T>();
					break;
				case SINGLE_CACHEOPT:
					this->queueImp = new LockfreeSingleQueueCacheOpt<T>(maxEntries);
					break;
				case SINGLE_BASIC:
					this->queueImp = new LockfreeSingleQueueBasic<T>(maxEntries);
					break;
				case SINGLE_CACHEOPT_LOCAL:
					this->queueImp = new LockfreeSingleQueueCacheOptLocal<T>(maxEntries);
					break;
				case MULTI:
					this->queueImp = new LockfreeMultiQueue<T>(maxEntries);
					break;
				default:
					THROWEXCEPTION("Unknown Queue Type");
			}
		};

		~ConcurrentQueue()
		{
			if(getCount() != 0) {
				msg(MSG_DEBUG, "WARNING: freeing non-empty queue - got count: %d", getCount());
			}
		};

		void setOwner(std::string name)
		{
			ownerName = name;
		}

		inline void push(T t)
		{
			DPRINTFL(MSG_VDEBUG, "(%s) trying to push element (%d elements in queue)", ownerName.c_str(), getCount());
#if defined(DEBUG)
			bool waiting = false;
			if (pushSemaphore.getCount() == 0) {
				waiting = true;
				DPRINTFL(MSG_DEBUG, "(%s) queue is full with %d elements, waiting ...", ownerName.c_str(), getCount());
			}
#endif
			if (!pushSemaphore.wait()) {
				DPRINTF("(%s) failed to push element, program is being shut down?", ownerName.c_str());
				return;
			}
#if defined(DEBUG)
			if (waiting) DPRINTF("(%s) pushing element now", ownerName.c_str());
#endif

			if(!this->queueImp->push(t))
				THROWEXCEPTION("Could not push element");
			pushedCount++;
			count++;

			popSemaphore.post();
			DPRINTFL(MSG_VDEBUG, "(%s) element pushed (%d elements in queue)", ownerName.c_str(), maxEntries-pushSemaphore.getCount(), pushSemaphore.getCount(), maxEntries);
		};

		inline bool pop(T* res)
		{
			DPRINTFL(MSG_VDEBUG, "(%s) trying to pop element (%d elements in queue)",
					(ownerName.empty() ? "<owner not set>" : ownerName.c_str()),
					maxEntries-pushSemaphore.getCount());

			if (!popSemaphore.wait()) {
				DPRINTF("(%s) failed to pop element, program is being shut down?", ownerName.c_str());
				return false;
			}

			if(!this->queueImp->pop(res))
				THROWEXCEPTION("Could not pop element");
			poppedCount++;
			count--;

			pushSemaphore.post();

			DPRINTFL(MSG_VDEBUG, "(%s) element popped", ownerName.c_str());

			return true;
		};

		// try to pop an entry from the queue before timeout occurs
		// if successful, res will hold the popped entry and true will be returned
		// of the timeout has been reached, res will be set to NULL and false will be returned
		inline bool pop(long timeout_ms, T *res)
		{
			DPRINTFL(MSG_VDEBUG, "(%s) trying to pop element (%d elements in queue)", ownerName.c_str(), getCount());
			// try to get an item from the queue
			if(!popSemaphore.wait(timeout_ms)) {
				// timeout occured
				DPRINTFL(MSG_VDEBUG, "(%s) timeout", ownerName.c_str());
				return false;
			}

			// popSemaphore.wait() succeeded, now pop the frontmost element
			if(!this->queueImp->pop(res))
				THROWEXCEPTION("Could not pop element");
			poppedCount++;
			count--;

			pushSemaphore.post();

			DPRINTFL(MSG_VDEBUG, "(%s) element popped", ownerName.c_str());

			return true;
		}

		// like pop above, but with absolute time instead of delta.
		// use this instead of the above, makes things easier!
		inline bool popAbs(const struct timeval &timeout, T *res)
		{
			DPRINTFL(MSG_VDEBUG, "(%s) trying to pop element (%d elements in queue)", ownerName.c_str(), getCount());
			
			if (!popSemaphore.waitAbs(timeout)) {
				// timeout occured
				DPRINTFL(MSG_VDEBUG, "(%s) timeout or program shutdown", ownerName.c_str());
				*res = 0;
				return false;
			}

			// popSemaphore.waitAbs() succeeded, now pop the frontmost element
			if(!this->queueImp->pop(res))
				THROWEXCEPTION("Could not pop element");
			poppedCount++;
			count--;

			pushSemaphore.post();

			DPRINTFL(MSG_VDEBUG, "(%s) element popped", ownerName.c_str());

			return true;
		}
		
		// like pop above, but with absolute time instead of delta.
		// use this instead of the above, makes things easier!
		inline bool popAbs(const struct timespec& timeout, T *res)
		{
			DPRINTFL(MSG_VDEBUG, "(%s) trying to pop element (%d elements in queue)", ownerName.c_str(), getCount());
		
			if (!popSemaphore.waitAbs(timeout)) {
				// timeout occured
				DPRINTFL(MSG_VDEBUG, "(%s) timeout or program shutdown", ownerName.c_str());
				*res = 0;
		
				return false;
			}

			// popSemaphore.waitAbs() succeeded, now pop the frontmost element
			if(!this->queueImp->pop(res))
				THROWEXCEPTION("Could not pop element");
			poppedCount++;
			count--;

			pushSemaphore.post();

			DPRINTFL(MSG_VDEBUG, "(%s) element popped", ownerName.c_str());

			return true;
		}

		inline int getCount() const
		{
			return count;
		};
		
		
		/**
		 * after calling this function, queue will not block again but return
		 * all functions with an error
		 * (useful for shutdown of this instance)
		 */
		void notifyShutdown() 
		{
			popSemaphore.notifyShutdown();
			pushSemaphore.notifyShutdown();
		}
		
		
		/**
		 * activates all thread-locking functionality inside the queue again
		 */
		void restart()
		{
			popSemaphore.restart();
			pushSemaphore.restart();
		}

		int pushedCount;
		int poppedCount;
		int maxEntries;

	protected:
		TimeoutSemaphore popSemaphore;
		TimeoutSemaphore pushSemaphore;
		std::string ownerName;
		volatile int count;

};

template<class T>
class ConcurrentQueueCond : public BaseConcurrentQueue<T>
{
	public:
		/**
		 * default queue size
		 */
		static const int DEFAULT_QUEUE_SIZE = 1000;
		static const int DEFUALT_SPINLOCK_TIMEOUT = 100;


		ConcurrentQueueCond(int qType = STL, int maxEntries = DEFAULT_QUEUE_SIZE, int spinLockTimeout = DEFUALT_SPINLOCK_TIMEOUT)
		{
			this->maxEntries = maxEntries;

			switch(qType){
				case STL:
					this->queueImp = new STLQueue<T>();
					break;
				case SINGLE_CACHEOPT:
					this->queueImp = new LockfreeSingleQueueCacheOpt<T>(maxEntries);
					break;
				case SINGLE_BASIC:
					this->queueImp = new LockfreeSingleQueueBasic<T>(maxEntries);
					break;
				case SINGLE_CACHEOPT_LOCAL:
					this->queueImp = new LockfreeSingleQueueCacheOptLocal<T>(maxEntries);
					break;
				case MULTI:
					this->queueImp = new LockfreeMultiQueue<T>(maxEntries);
					break;
				default:
					THROWEXCEPTION("Unknown Queue Type");
			}


			//initialize variable in cachelines
			void* tmp;
			uint32_t clsize = getCachelineSize();
			if(sizeof(pthread_cond_t) > clsize)
				THROWEXCEPTION("Error: Cacheline Size is not big enough");

			//producers variables
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			pushedCount = (uint32_t*)tmp;
			spinLockTimeoutProducer = pushedCount + 1;
			*pushedCount = 0;
			*spinLockTimeoutProducer = spinLockTimeout;

			//consumer variables
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			poppedCount = (uint32_t*)tmp;
			spinLockTimeoutConsumer = poppedCount + 1;
			*poppedCount = 0;
			*spinLockTimeoutConsumer = spinLockTimeout;

			//mutex and condition variables
			//TODO dynamically arrange to Cache Lines (maybe Cache Line is too small)
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			emptyMutex = (pthread_mutex_t*)tmp;
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			emptyCond = (pthread_cond_t*)tmp;
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			fullMutex = (pthread_mutex_t*)tmp;
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			fullCond = (pthread_cond_t*)tmp;
		};

		~ConcurrentQueueCond()
		{
			if(getCount() != 0) {
				msg(MSG_DEBUG, "WARNING: freeing non-empty queue - got count: %d", getCount());
			}
		};

		void setOwner(std::string name)
		{
			ownerName = name;
		}

		inline void push(T t)
		{

			while(!this->queueImp->push(t)){
				//go to sleep till one element is popped
				struct timespec timeout;
				clock_gettime(CLOCK_REALTIME, &timeout);
				timeout.tv_nsec += *spinLockTimeoutConsumer;

				if (pthread_mutex_lock (fullMutex) != 0)
					THROWEXCEPTION("lock of fullMutex failed");

				int ret = pthread_cond_timedwait (fullCond, fullMutex, &timeout);
				if(ret != 0 && ret != ETIMEDOUT)
					THROWEXCEPTION("fullCond wait failed");

				if (pthread_mutex_unlock (fullMutex) != 0)
					THROWEXCEPTION("unlock of fullMutex failed");
			}

			(*pushedCount)++;

			//wake up potential waiters
			if (pthread_mutex_lock (emptyMutex) != 0)
				THROWEXCEPTION("lock of emptyMutex failed");

			if (pthread_cond_signal (emptyCond) != 0)
				THROWEXCEPTION("emptyCond broadcast failed");

			if (pthread_mutex_unlock (emptyMutex) != 0)
				THROWEXCEPTION("unlock of emptyMutex failed");

		}

		inline bool pop(T* res)
		{

			while(!this->queueImp->pop(res)){
				//go to sleep till one element is pushed
				struct timespec timeout;
				clock_gettime(CLOCK_REALTIME, &timeout);
				timeout.tv_nsec += *spinLockTimeoutProducer;

				if (pthread_mutex_lock (emptyMutex) != 0)
					THROWEXCEPTION("lock of emptyutex failed");

				int ret = pthread_cond_timedwait (emptyCond, emptyMutex, &timeout);
				if(ret != 0 && ret != ETIMEDOUT)
					THROWEXCEPTION("emptyCond wait failed");

				if (pthread_mutex_unlock (emptyMutex) != 0)
					THROWEXCEPTION("unlock of emptyMutex failed");
			}

			(*poppedCount)++;

			//wake up potential waiters
			if (pthread_mutex_lock (fullMutex) != 0)
				THROWEXCEPTION("lock of fullMutex failed");

			if (pthread_cond_signal (fullCond) != 0)
				THROWEXCEPTION("fullCond broadcast failed");

			if (pthread_mutex_unlock (fullMutex) != 0)
				THROWEXCEPTION("unlock of fullMutex failed");

			return true;
		}

		// like pop above, but with absolute time instead of delta.
		// use this instead of the above, makes things easier!
		inline bool popAbs(const struct timespec& timeout, T *res)
		{
			// popSemaphore.waitAbs() succeeded, now pop the frontmost element
			while(!this->queueImp->pop(res)){
				//go to sleep till one element is pushed
				if (pthread_mutex_lock (emptyMutex) != 0)
					THROWEXCEPTION("lock of emptyutex failed");

				int ret = pthread_cond_timedwait (emptyCond, emptyMutex, &timeout);
				if(ret != 0){
					if(ret == ETIMEDOUT){
						if (pthread_mutex_unlock (emptyMutex) != 0)
							THROWEXCEPTION("unlock of emptyMutex failed");
						return false;
					}
					else
						THROWEXCEPTION("emptyCond wait failed");
				}

				if (pthread_mutex_unlock (emptyMutex) != 0)
					THROWEXCEPTION("unlock of emptyMutex failed");
			}

			(*poppedCount)++;

			//wake up potential waiters
			if (pthread_mutex_lock (fullMutex) != 0)
				THROWEXCEPTION("lock of fullMutex failed");

			if (pthread_cond_signal (fullCond) != 0)
				THROWEXCEPTION("fullCond broadcast failed");

			if (pthread_mutex_unlock (fullMutex) != 0)
				THROWEXCEPTION("unlock of fullMutex failed");

			return true;
		}

		inline int getCount() const
		{
			return *pushedCount - *poppedCount;
		}


		/**
		 * after calling this function, queue will not block again but return
		 * all functions with an error
		 * (useful for shutdown of this instance)
		 */
		void notifyShutdown(){	}

	protected:
		int maxEntries;

		//producer variables
		uint32_t* pushedCount;
		uint32_t* spinLockTimeoutProducer;
		//consumer variables
		uint32_t* poppedCount;
		uint32_t* spinLockTimeoutConsumer;

		//mutex and condition variables for waiting
		pthread_mutex_t* fullMutex;
		pthread_cond_t* fullCond;
		pthread_mutex_t* emptyMutex;
		pthread_cond_t* emptyCond;

		std::string ownerName;
};

template<class T>
class ConcurrentQueueSpinlock : public BaseConcurrentQueue<T>
{
	public:
		/**
		 * default queue size
		 */
		static const int DEFAULT_QUEUE_SIZE = 1000;
		static const uint32_t DEFAULT_SPINLOCK_TIMEOUT = 100;

		ConcurrentQueueSpinlock(int qType = STL, int maxEntries = DEFAULT_QUEUE_SIZE, uint32_t spinLockTimeout = DEFAULT_SPINLOCK_TIMEOUT)
		{
			this->maxEntries = maxEntries;

			//TOREMOVE
			void* tmp2;
			uint32_t clsize2 = getCachelineSize();
			if(posix_memalign(&tmp2, clsize2, clsize2) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			this->queueImp2 = (BaseQueue<T>**)tmp2;

			switch(qType){
				case STL:
					*(this->queueImp2) = new STLQueue<T>();
					break;
				case SINGLE_CACHEOPT:
					*(this->queueImp2) = new LockfreeSingleQueueCacheOpt<T>(maxEntries);
					break;
				case SINGLE_BASIC:
					*(this->queueImp2) = new LockfreeSingleQueueBasic<T>(maxEntries);
					break;
				case SINGLE_CACHEOPT_LOCAL:
					*(this->queueImp2) = new LockfreeSingleQueueCacheOptLocal<T>(maxEntries);
					break;
				case MULTI:
					*(this->queueImp2) = new LockfreeMultiQueue<T>(maxEntries);
					break;
				default:
					THROWEXCEPTION("Unknown Queue Type");
			}

			//initialize variable in cachelines
			void* tmp;
			uint32_t clsize = getCachelineSize();
			if(sizeof(uint32_t) + sizeof(struct timespec) > clsize)
				THROWEXCEPTION("Error: Cacheline Size is not big enough");

			//producers variables
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			pushedCount = (uint32_t*)tmp;
			spinLockTimeoutProducer = (struct timespec*)(pushedCount + 1);
			*pushedCount = 0;
			spinLockTimeoutProducer->tv_sec = 0;
			spinLockTimeoutProducer->tv_nsec = spinLockTimeout;

			//consumer variables
			if(posix_memalign(&tmp, clsize, clsize) != 0)
				THROWEXCEPTION("Error: posix_memalign()");
			poppedCount = (uint32_t*)tmp;
			spinLockTimeoutConsumer = (struct timespec*)(poppedCount + 1);
			*poppedCount = 0;
			spinLockTimeoutConsumer->tv_sec = 0;
			spinLockTimeoutConsumer->tv_nsec = spinLockTimeout;
		}

		~ConcurrentQueueSpinlock()
		{
			if(getCount() != 0) {
				msg(MSG_DEBUG, "WARNING: freeing non-empty queue - got count: %d", getCount());
			}
		}

		void setOwner(std::string name)
		{
			ownerName = name;
		}

		inline void push(T t)
		{

			while(!(*(this->queueImp2))->push(t)){
				nanosleep(spinLockTimeoutConsumer, NULL);
			}

			(*pushedCount)++;

		}

		inline bool pop(T* res)
		{

			while(!(*(this->queueImp2))->pop(res)){
				nanosleep(spinLockTimeoutProducer, NULL);
			}

			(*poppedCount)++;

			return true;
		}

		// like pop above, but with absolute time instead of delta.
		// use this instead of the above, makes things easier!
		inline bool popAbs(const struct timespec& timeout, T *res)
		{
			// popSemaphore.waitAbs() succeeded, now pop the frontmost element
			if(!(*(this->queueImp2))->pop(res)){
				nanosleep(&timeout, NULL);
				if(!this->queueImp->pop(res))
					return false;
			}

			(*poppedCount)++;

			return true;
		}

		inline int getCount() const
		{
			return *pushedCount - *poppedCount;
		};


		/**
		 * after calling this function, queue will not block again but return
		 * all functions with an error
		 * (useful for shutdown of this instance)
		 */
		void notifyShutdown(){ }


	protected:
		//producer variables
		uint32_t* pushedCount;
		struct timespec* spinLockTimeoutProducer;
		//consumer variables
		uint32_t* poppedCount;
		struct timespec* spinLockTimeoutConsumer;

		int maxEntries;
		std::string ownerName;
};

#endif
