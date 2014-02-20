package io.osv.util.json;

/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import javax.management.AttributeNotFoundException;
import javax.management.InstanceNotFoundException;
import javax.management.MBeanAttributeInfo;
import javax.management.MBeanException;
import javax.management.MBeanServer;
import javax.management.ObjectName;
import javax.management.ReflectionException;
import javax.management.openmbean.CompositeData;

/**
 * This is a helper class to map types to json format.
 *
 */
public class JsonGenerator {
	/**
	 * Recursively map an attribute value to string
	 * 
	 * @param value
	 *            an attribute value
	 * @return a string representative of the value
	 */
	public static String attrValueToString(Object value) {
		if (value instanceof CompositeData[]) {
			CompositeData data[] = (CompositeData[]) value;
			ArrayBuilder sb = new ArrayBuilder();
			for (int i = 0; i < data.length; i++) {
				sb.append(compositeToString(data[i]));
			}
			return sb.toString();
		}
		if (value instanceof CompositeData) {
			return compositeToString((CompositeData) value);
		}
		if (value instanceof String[]) {
			String vals[] = (String[]) value;
			ArrayBuilder sb = new ArrayBuilder();
			for (int i = 0; i < vals.length; i++) {
				sb.append("\"" + vals[i] + "\"");
			}
			return sb.toString();
		}
		if (value instanceof Long || value instanceof Integer) {
			return value.toString();
		}
		return (value == null) ? "\"\"" : "\"" + value.toString() + "\"";
	}

	/**
	 * Recursively map a composite value to a string
	 * 
	 * @param data
	 *            Composite Data value
	 * @return a string representation of the data
	 */
	public static String compositeToString(CompositeData data) {
		ArrayBuilder sb = new ArrayBuilder("{", "}");
		for (String key : data.getCompositeType().keySet()) {
			sb.append("\"").append(key).append("\": ")
					.append(attrValueToString(data.get(key)));
		}
		return sb.toString();
	}

	/**
	 * Map an attribute to a string and add its value if present
	 * 
	 * @param att
	 *            the mbeanServer attribute
	 * @param objName
	 *            the mbeanServer object name
	 * @param mbeanServer
	 *            an mbeanServer server
	 * @return a string representative of the attribute in a JSON format
	 * @throws ReflectionException
	 * @throws MBeanException
	 * @throws InstanceNotFoundException
	 * @throws AttributeNotFoundException
	 */
	public static String attrToString(MBeanAttributeInfo att,
			ObjectName objName, MBeanServer mbeanServer)
			throws AttributeNotFoundException, InstanceNotFoundException,
			MBeanException, ReflectionException {
		String value = "";
		value = (att.isReadable()) ? attrValueToString(mbeanServer
				.getAttribute(objName, att.getName())) : "\"\"";

		return "{\"name\": \"" + att.getName() + "\", \"type\": \""
				+ att.getType() + "\", \"value\": " + value
				+ ", \"writable\": " + Boolean.toString(att.isWritable())
				+ ", \"description\": \"" + att.getDescription() + "\"}";
	}
}
