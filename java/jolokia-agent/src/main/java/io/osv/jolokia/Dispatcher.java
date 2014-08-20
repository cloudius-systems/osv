package io.osv.jolokia;

import java.io.ByteArrayInputStream;

import javax.management.RuntimeMBeanException;

import org.jolokia.backend.BackendManager;
import org.jolokia.config.ConfigKey;
import org.jolokia.config.Configuration;
import org.jolokia.http.HttpRequestHandler;
import org.jolokia.util.LogHandler;
import org.json.simple.JSONAware;

public class Dispatcher {
	private final HttpRequestHandler handler;

	public Dispatcher() {
		LogHandler h = new LogHandler() {
			public void debug(String message) {
			}

			public void info(String message) {
			}

			public void error(String message, Throwable t) {
				System.err.println(message);
				t.printStackTrace(System.err);
			}
		};
		Thread t = Thread.currentThread();
		ClassLoader l = t.getContextClassLoader();
		try {
			t.setContextClassLoader(getClass().getClassLoader());
			Configuration cfg = new Configuration(ConfigKey.AGENT_ID,
					"OSv Jolokia Bridge");
			BackendManager mgr = new BackendManager(cfg, h);
			handler = new HttpRequestHandler(cfg, mgr, h);
		} finally {
			t.setContextClassLoader(l);
		}
	}

	public int dispatch(RequestAndResponse req) {
		int returnCode = 200;
		JSONAware json = null;
		try {
			switch (req.getMode()) {
			case RequestAndResponse.GET_REQUEST:
				json = handler.handleGetRequest(req.getUri(), req.getPath(),
						req.getParameters());
				break;
			case RequestAndResponse.POST_REQUEST:
				json = handler.handlePostRequest(req.getUri(),
						new ByteArrayInputStream(req.getBody()),
						req.getEncoding(), req.getParameters());

				break;
			}
		} catch (Throwable exp) {
			json = handler
					.handleThrowable(exp instanceof RuntimeMBeanException ? ((RuntimeMBeanException) exp)
							.getTargetException() : exp);
		} finally {
			String callback = req
					.getParameter(ConfigKey.CALLBACK.getKeyValue());
			String answer = json != null ? json.toJSONString()
					: handler
							.handleThrowable(
									new Exception(
											"Internal error while handling an exception"))
							.toJSONString();
			String mimeType = null;
			if (callback != null) {
				answer = callback + "(" + answer + ");";
			} else {
				mimeType = req.getParameter(ConfigKey.MIME_TYPE.getKeyValue());
			}
			req.setMimeType(mimeType);
			req.setResponse(answer);
		}
		// Note. Vanilla Jolokia connectors (Servlet etc) always give back
		// 200, even if a call fails. I.e. caller must check the response body
		// for the actual operation results.
		// Ensure we keep this behaviour for client compatibility.
		return returnCode;
	}
}
