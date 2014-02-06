package io.osv;

import com.sun.management.GarbageCollectionNotificationInfo;

import javax.management.*;
import javax.management.openmbean.CompositeData;
import java.lang.management.GarbageCollectorMXBean;
import java.lang.management.ManagementFactory;
import java.util.List;

public class OSvGCMonitor {

    static {
        System.load("/usr/lib/jni/monitor.so");
    }

    private native static void NotifyOSv(long h);

    static long handle;

    static void MonitorGC(long h) {

        handle = h;
        List<GarbageCollectorMXBean> gcbeans = java.lang.management.ManagementFactory.getGarbageCollectorMXBeans();
        MBeanServer server = ManagementFactory.getPlatformMBeanServer();
        try {
            ObjectName gcName = new ObjectName(ManagementFactory.GARBAGE_COLLECTOR_MXBEAN_DOMAIN_TYPE + ",*");
            for (ObjectName name : server.queryNames(gcName, null)) {
                GarbageCollectorMXBean gc = ManagementFactory.newPlatformMXBeanProxy(server, name.getCanonicalName(), GarbageCollectorMXBean.class);
                gcbeans.add(gc);

                NotificationEmitter emitter = (NotificationEmitter) gc;
                NotificationListener listener = new NotificationListener() {
                    @Override
                    public void handleNotification(Notification notification, Object handback) {
                        if (notification.getType().equals(GarbageCollectionNotificationInfo.GARBAGE_COLLECTION_NOTIFICATION)) {
                            CompositeData ndata = (CompositeData) notification.getUserData();
                            GarbageCollectionNotificationInfo info = GarbageCollectionNotificationInfo.from(ndata);
                            // FIXME: This will relieve pressure, but only
                            // after the GC is run. Ideally, we should be able
                            // to flush our balloons *before* the GC is run,
                            // and potentially avoid it. But there is no
                            // indication from the JVM that a full GC is about
                            // to happen, only that it has just happened. In
                            // the future, we may be able to do better by
                            // tracking minor GCs and trying to predict if a
                            // major GC is eminent.
                            if ("end of major GC".equals(info.getGcAction())) {
                                NotifyOSv(handle); // FIXME: Pass parameters here to better inform OSv.
                            }
                        }
                    }
                };
                emitter.addNotificationListener(listener, null, null);
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
