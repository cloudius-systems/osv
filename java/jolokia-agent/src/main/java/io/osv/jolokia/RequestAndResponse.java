package io.osv.jolokia;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public class RequestAndResponse {
	public static final int GET_REQUEST = 0;
	public static final int POST_REQUEST = 1;

	private int mode;
	private final String uri;
	private final String path;
	private final String encoding;
	private final byte[] body;
	private String response;
	private String mimeType;
	private Map<String, String[]> parameters;

	public RequestAndResponse(int mode, String uri, String path, String enc, byte[] body) {
		this.uri = uri;
		this.mode = mode;
		this.path = path;
		this.encoding = enc;
		this.body = body;
	}

	public RequestAndResponse(int mode, String uri, String path) {
		this(mode, uri, path, null, null);
	}

	public void addParameter(String name, String value) {
		if (parameters == null) {
			parameters = new HashMap<String, String[]>();
		}
		String[] values = parameters.get(name);
		parameters.put(name, add(values, value));
	}

	private String[] add(String values[], String value) {
		if (value == null) {
			return values != null ? values : new String[0];
		}
		values = values != null ? Arrays.copyOf(values, values.length + 1) : new String[1];
		values[values.length - 1] = value;
		return values;
	}

	public String getUri() {
		return uri;
	}

	public int getMode() {
		return mode;
	}

	public void setMode(int mode) {
		this.mode = mode;
	}

	public String getPath() {
		return path;
	}

	public String getResponse() {
		return response;
	}

	public void setResponse(String response) {
		this.response = response;
	}

	public String getMimeType() {
		return mimeType;
	}

	public void setMimeType(String mimeType) {
		this.mimeType = mimeType;
	}

	public Map<String, String[]> getParameters() {
		return parameters;
	}

	public void setParameters(Map<String, String[]> parameters) {
		this.parameters = parameters;
	}

	public String getParameter(String key) {
		String vals[] = parameters != null ? parameters.get(key) : null;
		return vals != null ? vals[0] : null;
	}

	public byte[] getBody() {
		return body;
	}

	public String getEncoding() {
		return encoding;
	}
}
