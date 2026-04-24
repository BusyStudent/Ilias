#include <ilias/platform/qt.hpp>
#include <QTest>

// Prepare the Test object
class Testing : public QObject {
Q_OBJECT
public:
    Testing();
signals:
    void notify(); // Used for test the QSignal
private slots:
    void testTask();
    void testTimer();
    void testTcp();
    void testSignal();
private:
    ilias::QIoContext mCtxt;
};