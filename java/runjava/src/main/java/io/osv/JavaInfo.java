package io.osv;

import io.osv.util.json.ArrayBuilder;
import io.osv.util.json.JsonGenerator;
import java.lang.management.GarbageCollectorMXBean;
import java.util.List;
import java.util.Set;
import javax.management.Attribute;
import javax.management.AttributeNotFoundException;
import javax.management.InstanceNotFoundException;
import javax.management.IntrospectionException;
import javax.management.InvalidAttributeValueException;
import javax.management.MBeanAttributeInfo;
import javax.management.MBeanException;
import javax.management.MBeanInfo;
import javax.management.MalformedObjectNameException;
import javax.management.ObjectName;
import javax.management.ReflectionException;

/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

public class JavaInfo {
	/**
	 * Get an Mbean description and data
	 * 
	 * @param mbeanName
	 *            the MBean name
	 * @return a string in a JSON format representative of the MBean
	 * @throws MalformedObjectNameException
	 * @throws ReflectionException
	 * @throws InstanceNotFoundException
	 * @throws IntrospectionException
	 * @throws MBeanException
	 * @throws AttributeNotFoundException
	 */
	public static String getMbean(String mbeanName)
			throws MalformedObjectNameException, IntrospectionException,
			InstanceNotFoundException, ReflectionException,
			AttributeNotFoundException, MBeanException {
		javax.management.MBeanServer mbeanServer = java.lang.management.ManagementFactory
				.getPlatformMBeanServer();
		ObjectName objName = new ObjectName(mbeanName);
		MBeanInfo res = mbeanServer.getMBeanInfo(objName);

		MBeanAttributeInfo[] att = res.getAttributes();
		ArrayBuilder sb = new ArrayBuilder();
		for (int i = 0; i < att.length; i++) {
			try {
				sb.append(JsonGenerator.attrToString(att[i], objName,
						mbeanServer));
			} catch (javax.management.RuntimeMBeanException e) {
				// some of the attribute can throw UnsupportedOperationException
				// even if
				// they have read permission
			}
		}
		return sb.toString();
	}

	/**
	 * Set an MBean attribute to a new value
	 * 
	 * @param mbeanName
	 *            the MBean name
	 * @param attribute
	 *            the attribute name, the attribute use an xpath syntax to
	 *            define the full path into the attribute
	 * @param value
	 *            the value to set
	 * @throws MalformedObjectNameException
	 * @throws ReflectionException
	 * @throws MBeanException
	 * @throws InstanceNotFoundException
	 * @throws AttributeNotFoundException
	 * @throws InvalidAttributeValueException
	 */
	public static void setMbean(String mbeanName, String attribute, String value)
			throws MalformedObjectNameException, AttributeNotFoundException,
			InstanceNotFoundException, MBeanException, ReflectionException,
			InvalidAttributeValueException {
		javax.management.MBeanServer mbeanServer = java.lang.management.ManagementFactory
				.getPlatformMBeanServer();
		ObjectName objName = new ObjectName(mbeanName);
		Object attr = mbeanServer.getAttribute(objName, attribute);
		Object valueObject = getUpdatedAttribute(attr, value);
		if (valueObject != null) {
			mbeanServer.setAttribute(objName, new Attribute(attribute,
					valueObject));
		}
	}

	/**
	 * A helper method to get an updated attribute from an existing one. It uses
	 * the original attribute determine the type the value should be mapped to.
	 * 
	 * @param attr
	 *            the original attribute
	 * @param value
	 *            the new value
	 * @return the new attribute
	 */
	private static Object getUpdatedAttribute(Object attr, String value) {
		if (attr instanceof Long) {
			return Long.parseLong(value);
		}
		if (attr instanceof String) {
			return value;
		}
		if (attr instanceof Boolean) {
			return Boolean.valueOf(value);
		}
		if (attr instanceof Integer) {
			return Integer.parseInt(value);
		}
		return null;
	}

	/**
	 * Get a list of all available MBean names.
	 * 
	 * @return an array of string with all the mbeanServer names
	 */
	public static String[] getAllMbean() {
		javax.management.MBeanServer mbeanServer = java.lang.management.ManagementFactory
				.getPlatformMBeanServer();
		Set<ObjectName> instances = mbeanServer.queryNames(null, null);
		String[] res = new String[instances.size()];
		int i = 0;
		for (ObjectName obj : instances) {
			res[i++] = obj.getCanonicalName();
		}

		return res;
	}

	/**
	 * Get garbage collector information
	 * 
	 * @return an array of GCInfo object
	 */
	public static GCInfo[] getAllGC() {
		List<GarbageCollectorMXBean> gcCollection = java.lang.management.ManagementFactory
				.getGarbageCollectorMXBeans();
		GCInfo[] res = new GCInfo[gcCollection.size()];
		int i = 0;
		for (GarbageCollectorMXBean gc : gcCollection) {
			GCInfo info = new GCInfo();
			info.count = gc.getCollectionCount();
			info.name = gc.getName();
			info.time = gc.getCollectionTime();
			info.pools = gc.getMemoryPoolNames();
			res[i++] = info;

		}
		return res;
	}

	/**
	 * Get a system property
	 * 
	 * @param str
	 *            a system property name
	 * @return the system property value
	 */
	public static String getProperty(String str) {
		return System.getProperty(str);
	}

}
