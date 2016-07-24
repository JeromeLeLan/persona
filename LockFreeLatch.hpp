#include <atomic>

#define USE_RETRY_COUNT	// Comment to avoid taking time counting read loops
#define CACHE_LINE_SIZE 1024

template<typename Type>
class LockFreeLatch {

public:
	LockFreeLatch(Type const & src) : positions(Positions<1, 1>::val), retryCnt(0) {
		memcpy(&buffer_1, &src, sizeof(Type));
	}

	// Return pointer to safely write data in shared structure
	Type * acquireWrite() {

		/* We don't need any infinite loop to poll on positions, one of these 9 tests will be true.
		 * Because at worst, if read is called at the same time,
		 * positions' value stabilizes on <0,0>, <1,1> or <2,2>.
		 */
		if (positions == Positions<1, 2>::val)	return &buffer_3;	// You can safely write in 3
		if (positions == Positions<1, 3>::val)	return &buffer_2;	// You can safely write in 2
		if (positions == Positions<2, 3>::val)	return &buffer_1;	// You can safely write in 2
		if (positions == Positions<2, 1>::val)	return &buffer_3;	// You can safely write in 3
		if (positions == Positions<3, 1>::val)	return &buffer_2;	// You can safely write in 2
		if (positions == Positions<3, 2>::val)	return &buffer_1;	// You can safely write in 1

		// If read() changed the status during the first 6 tests, it's for one of these three
		if (positions == Positions<1, 1>::val)	return &buffer_2;	// You can safely write in 2
		if (positions == Positions<2, 2>::val)	return &buffer_3;	// You can safely write in 3
		if (positions == Positions<3, 3>::val)	return &buffer_1;	// You can safely write in 1

		return NULL;	// Should not happen
	}


	// Use to notify finish writing after getting the pointer of the writable block
	void releaseWrite() {

		char expected;
		// Once again, the infinite loop here is not necessary because of the same reasons as above
		expected = Positions<1, 2>::val;
		if (positions.compare_exchange_weak(expected, Positions<3, 2>::val)) return;	// Writer have written in 3
		expected = Positions<1, 2>::val;
		if (positions.compare_exchange_weak(expected, Positions<2, 3>::val)) return;	// Writer have written in 2
		expected = Positions<2, 1>::val;
		if (positions.compare_exchange_weak(expected, Positions<3, 1>::val)) return;	// Writer have written in 3
		expected = Positions<2, 3>::val;
		if (positions.compare_exchange_weak(expected, Positions<1, 3>::val)) return;	// Writer have written in 1
		expected = Positions<3, 1>::val;
		if (positions.compare_exchange_weak(expected, Positions<2, 1>::val)) return;	// Writer have written in 2
		expected = Positions<2, 2>::val;
		if (positions.compare_exchange_weak(expected, Positions<1, 2>::val)) return;	// Writer have written in 1

		// If read() changed the status during the first 6 tests, it's for one of these three
		expected = Positions<1, 1>::val;
		if (positions.compare_exchange_weak(expected, Positions<2, 1>::val)) return;	// Writer have written in 2
		expected = Positions<2, 2>::val;
		if (positions.compare_exchange_weak(expected, Positions<3, 2>::val)) return;	// Writer have written in 3
		expected = Positions<3, 3>::val;
		if (positions.compare_exchange_weak(expected, Positions<1, 3>::val)) return;	// Writer have written in 1
	}


	// Return pointer where to safely read data in shared structure
	Type * read() {
#ifdef USE_RETRY_COUNT
		retryCnt = 0;
#endif
		// These three positions can not be set by releaseWrite(), we only need to check them once
		if (positions == Positions<1, 1>::val)	return &buffer_1;	// You can safely read in 1
		if (positions == Positions<2, 2>::val)	return &buffer_2;	// You can safely read in 2
		if (positions == Positions<3, 3>::val)	return &buffer_3;	// You can safely read in 3
		
		/* This infinite loop should iterate only once, except if we are really really unlucky.
		 * It's here because conceptually, every tests in one loop could fail if releaseWrite() is called synchronously with the tests.
		 */
		for(;;) {
			// All these positions can be set by releaseWrite(), we may need to check them multiple times
			char expected;
			expected = Positions<2, 1>::val;
			if (positions.compare_exchange_weak(expected, Positions<2, 2>::val)) return &buffer_2;	// You can safely read in 2
			expected = Positions<3, 1>::val;
			if (positions.compare_exchange_weak(expected, Positions<3, 3>::val)) return &buffer_3;	// You can safely read in 3
			// Retry the last two if releaseWrite() was called simultaneously
			expected = Positions<2, 1>::val;
			if (positions.compare_exchange_weak(expected, Positions<2, 2>::val)) return &buffer_2;	// You can safely read in 2
			expected = Positions<3, 1>::val;
			if (positions.compare_exchange_weak(expected, Positions<3, 3>::val)) return &buffer_3;	// You can safely read in 3

			expected = Positions<1, 2>::val;
			if (positions.compare_exchange_weak(expected, Positions<1, 1>::val)) return &buffer_1;	// You can safely read in 1
			expected = Positions<3, 2>::val;
			if (positions.compare_exchange_weak(expected, Positions<3, 3>::val)) return &buffer_3;	// You can safely read in 3
			// Retry the last two if releaseWrite() was called simultaneously
			expected = Positions<1, 2>::val;
			if (positions.compare_exchange_weak(expected, Positions<1, 1>::val)) return &buffer_1;	// You can safely read in 1
			expected = Positions<3, 2>::val;
			if (positions.compare_exchange_weak(expected, Positions<3, 3>::val)) return &buffer_3;	// You can safely read in 3

			expected = Positions<1, 3>::val;
			if (positions.compare_exchange_weak(expected, Positions<1, 1>::val)) return &buffer_1;	// You can safely read in 1
			expected = Positions<2, 3>::val;
			if (positions.compare_exchange_weak(expected, Positions<2, 2>::val)) return &buffer_2;	// You can safely read in 2
			// Retry the last two if releaseWrite() was called simultaneously
			expected = Positions<1, 3>::val;
			if (positions.compare_exchange_weak(expected, Positions<1, 1>::val)) return &buffer_1;	// You can safely read in 1
			expected = Positions<2, 3>::val;
			if (positions.compare_exchange_weak(expected, Positions<2, 2>::val)) return &buffer_2;	// You can safely read in 2

#ifdef USE_RETRY_COUNT
			++retryCnt;
#endif
		}
	}

#ifdef USE_RETRY_COUNT
	// Useful to monitor number of retries in infinite loop of previous read() call
	int getRetryCount() const { return retryCnt; }
#endif

private:
	// Positions' value contains the current readable item returned by read() and the current non-writable item
	template <char readableItem, char nonWritableItem>
	struct Positions {
		enum { val = (readableItem << 4) | nonWritableItem };
	};

	volatile std::atomic_char positions;

	int		retryCnt;

	Type	buffer_1		alignas(CACHE_LINE_SIZE);
	Type	buffer_2		alignas(CACHE_LINE_SIZE);
	Type	buffer_3		alignas(CACHE_LINE_SIZE);	// To avoid false sharing
};
