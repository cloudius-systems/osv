package io.osv;

import com.sun.management.GarbageCollectionNotificationInfo;

import javax.management.*;
import javax.management.openmbean.CompositeData;
import java.lang.management.GarbageCollectorMXBean;
import java.lang.management.ManagementFactory;
import java.util.List;
import java.lang.Runtime;

public class OSvGCMonitor {

    static {
        System.load("/usr/lib/jni/monitor.so");
    }

    private native static void NotifyOSv(long h, long qty);

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
                            boolean major = "end of major GC".equals(info.getGcAction());
                            long free = Runtime.getRuntime().freeMemory();
                            long total = Runtime.getRuntime().totalMemory();
                            long qty = (15 * total) - (free * 100);
                            if (qty > 0) {
                                NotifyOSv(handle, qty);
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
