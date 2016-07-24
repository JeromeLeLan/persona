#include <chrono>
#include <thread>
#include <iostream>
#include "../LockFreeLatch.hpp"

using namespace std;

typedef std::chrono::high_resolution_clock Timer;

typedef long long Type;

template <int size>
using Array = long long[size];

template <int size>
using Latch = LockFreeLatch<Array<size>>;


template<int size, int loopIteration>
class LockFreeLatchProducer {
public:
	LockFreeLatchProducer(Latch<size> & latch) : looping(true), error(false) {
		thread = std::thread([&, this]() { Loop(latch, looping, error); });
	}

	bool isRunning() {
		return looping;
	}

	bool errorDetected() {
		return error;
	}

	void join() {
		thread.join();
		std::cout << "LockFreeLatch Producer stopped!" << std::endl;
	}

private:
	std::thread	thread;
	bool		looping;
	bool		error;

	static void Loop(Latch<size> & latch, bool & looping, bool & error) {
		unsigned long long		loop(0);
		long long				maxAcquireWrite(0);
		long long				maxReleaseWrite(0);

		std::cout << "LockFreeLatch Producer started!" << std::endl;

		while (loop < loopIteration) {

			++loop;

			auto t0 = Timer::now();
			Array<size> * arrayToWrite = latch.acquireWrite();
			auto t1 = Timer::now();

			std::chrono::microseconds delay = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
			if (maxAcquireWrite < delay.count()) {
				maxAcquireWrite = delay.count();
			}
			if (!arrayToWrite) {
				error = true;
				looping = false;
				std::cout << "ERROR: acquireWrite returned NULL pointer!" << std::endl;
				break;
			}

			for (int i = 0; i < size; ++i) {
				(*arrayToWrite)[i] = loop;
			}

			t0 = Timer::now();
			latch.releaseWrite();
			t1 = Timer::now();

			delay = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
			if (maxReleaseWrite < delay.count()) {
				maxReleaseWrite = delay.count();
			}
		}

		std::cout << "Producer maximum call time : acquireWrite() = " << maxAcquireWrite << " us, releaseWrite() = " << maxReleaseWrite << " us" << std::endl;
		looping = false;
	}
};

	
template<int size, int loopIteration>
class LockFreeLatchConsumer {
public:
	LockFreeLatchConsumer(Latch<size> & latch) : looping(true), error(false) {
		thread = std::thread([&, this]() { Loop(latch, looping, error); });
	}

	void join() {
		looping = false;
		thread.join();
		std::cout << "LockFreeLatch Consumer stopped!" << std::endl;
	}

	bool isRunning() {
		return looping;
	}

	bool errorDetected() {
		return error;
	}

private:
	std::thread		thread;
	bool			looping;
	bool			error;

	static void Loop(Latch<size> & latch, bool & looping, bool & error) {
		unsigned long long		loop(0);
		unsigned long long		changeCount(0);
		Type					prevVal(0);
		long long				maxRead(0);
		unsigned int			readLoopCnt;
		unsigned int			maxReadLoopCnt(0);

		std::cout << "LockFreeLatch Consumer started!" << std::endl;

		while (looping) {

			++loop;

			auto t0 = Timer::now();
			Array<size> * arrayToRead = latch.read();
			auto t1 = Timer::now();

			std::chrono::microseconds delay = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
			if (maxRead < delay.count()) {
				maxRead = delay.count();
			}

			readLoopCnt = latch.getRetryCount();
			if (maxReadLoopCnt < readLoopCnt) {
				maxReadLoopCnt = readLoopCnt;
			}
			if (!arrayToRead) {
				error = true;
				looping = false;
				std::cout << "ERROR: read returned NULL pointer!" << std::endl;
				return;
			}

			Type currVal = (*arrayToRead)[0];
			if (prevVal != currVal) {
				prevVal = currVal;
				changeCount++;
				if (prevVal > currVal) {
					error = true;
					looping = false;
					std::cout << "ERROR: Found value older than current value..." << std::endl;
					return;
				}
			}
				
			for(int i=0; i<size; ++i) {
				if (currVal != (*arrayToRead)[i]) {
					error = true;
					looping = false;
					std::cout << "ERROR: memory corruption on read buffer!" << std::endl;
					return;
				}
			}
		}
			
		std::cout << "Consumer maximum call time: read() = " << maxRead << " us" << std::endl;
		std::cout << maxReadLoopCnt << " loop(s) in read call" << std::endl;
		std::cout << changeCount << " changes detected out of " << loopIteration << " (" << 100 * changeCount / loopIteration << " percent)" << std::endl;
	}

};

template<int size>
void oneProducerOneConsumerTest() {

	const int loopIteration = 100000;

	Array<size> array = {};
	Latch<size> latch(array);
	LockFreeLatchConsumer<size, loopIteration> consumer(latch);
	while (!consumer.isRunning()) {
		std::this_thread::sleep_for(1ms);
	}

	std::this_thread::sleep_for(100ms);
	LockFreeLatchProducer<size, loopIteration> producer(latch);
	while (!producer.isRunning()) {
		std::this_thread::sleep_for(1ms);
	}

	while (producer.isRunning()) {
		std::this_thread::sleep_for(1ms);
	}

	// Wait for the last changes to be retrieved by the consumer
	std::this_thread::sleep_for(1s);
	consumer.join();
	producer.join();

	if (consumer.errorDetected()) {
		std::cout << "ERROR: Memory corruption happened with SmallArray structure" << std::endl;
	}
	if (producer.errorDetected()) {
		std::cout << "ERROR: Bad behaviour of acquireWrite method!" << std::endl;
	}
}


int main() {

	std::cout << "Testing LockFreeLatch of small array..." << std::endl;
	const int smallSize = 10;
	oneProducerOneConsumerTest<smallSize>();
	
	std::cout << "\nTesting LockFreeLatch of big array..." << std::endl;
	const int bigSize = 10000;
	oneProducerOneConsumerTest<bigSize>();

	system("pause");
	return 0;
}
