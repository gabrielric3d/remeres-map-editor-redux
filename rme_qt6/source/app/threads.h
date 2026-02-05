#ifndef RME_THREAD_H_
#define RME_THREAD_H_

#include <QThread>

class Thread : public QThread {
public:
	// kind is ignored in QThread wrapper for now, standard QThread behavior
	Thread(int /*kind*/ = 0) { }

	void Execute() { // Maps to Start
		start();
	}

protected:
    // Pure virtual in wxThread, virtual in QThread
    // Subclasses must implement run()
};

class JoinableThread : public Thread {
public:
	JoinableThread() : Thread() { }
};

class DetachedThread : public Thread {
public:
	DetachedThread() : Thread() { }
    // Detached in QThread usually means connecting finished->deleteLater
    // But for now we keep it simple.
};

#endif
