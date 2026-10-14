#pragma once

#include "keys.h"
#include "razerdevice.h"

#include <QObject>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct input_event;
struct libevdev_uinput;

// Remaps one physical Razer device.
//
// A worker thread reads every event node of the device. While "active" the
// nodes are grabbed exclusively (EVIOCGRAB) so the rest of the system no longer
// sees them, and events are re-emitted, with bindings applied, through a
// virtual uinput device. While inactive the nodes are only monitored so the UI
// can show live key presses and detect keys for new bindings.
class Remapper : public QObject
{
    Q_OBJECT

public:
    explicit Remapper(RazerDevice device, QObject *parent = nullptr);
    ~Remapper() override;

    const RazerDevice &device() const { return m_device; }

    bool start(QString *error);
    void stop();

    void setBindings(const BindingMap &bindings);
    void setActive(bool active);
    bool isGrabbed() const { return m_grabbed.load(); }
    bool isWaitingForRelease() const { return m_pendingGrab.load(); }

signals:
    // Raw input from the physical device, before remapping. Emitted from the
    // worker thread; connect with the default (queued) connection type.
    void inputEvent(int code, bool pressed);
    void stateChanged();
    void failed(const QString &message);
    void disconnected();

private:
    struct Source
    {
        int fd = -1;
        bool writable = false;
        bool hasLeds = false;
    };

    void run();
    bool anyKeyDown() const;
    bool grab();
    void ungrab();
    bool createVirtualDevice();
    bool readSource(const Source &src);
    void handleEvent(const input_event &ev);
    void handleWheel(const input_event &ev);
    void forwardLeds();

    void write(int type, int code, int value);
    void syn();
    void press(int code);
    void release(int code);
    void tap(const QList<int> &targets);
    QList<int> lookup(int code, bool *bound);

    RazerDevice m_device;
    std::vector<Source> m_sources;
    int m_wakePipe[2] = {-1, -1};
    std::thread m_thread;

    std::mutex m_bindingsMutex;
    BindingMap m_bindings;

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_grabbed{false};
    std::atomic<bool> m_pendingGrab{false};
    std::atomic<bool> m_quit{false};

    // Worker-thread state.
    libevdev_uinput *m_uinput = nullptr;
    QHash<int, int> m_outputCount;     // output code -> number of sources holding it
    QHash<int, QList<int>> m_held;     // source code -> outputs it pressed
};
