using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Balls.Ecs;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App
{

    public sealed class ControlApi : IDisposable
    {
        private const int ListenBacklog = 16;

        private sealed class Request
        {
            public string Path;
            public string Target;
            public Dictionary<string, string> Query;
            public int Status = 200;
            public string Body = "{}";
            public bool Answered;
            public readonly ManualResetEventSlim Done = new(false);
        }

        private readonly BallsApp app;

        private IntPtr control;
        private readonly TcpListener listener;
        private readonly Thread thread;
        private readonly Queue<(Socket socket, Request request)> queue = new();
        private readonly List<(string path, Request request, float deadline)> captures = new();
        private volatile bool running;
        private bool quitRequested;

        public ControlApi(BallsApp app, int port)
        {
            this.app = app;
            control = BallsNative.balls_control_create(app.LevelDirectoryFor(BallsApp.NormalMode),
                app.LevelDirectoryFor(BallsApp.PerformanceMode));
            listener = new TcpListener(IPAddress.Loopback, port);
            thread = new Thread(AcceptLoop) { IsBackground = true, Name = "balls-control" };
        }

        public int Port { get; private set; }

        public void Start()
        {
            try
            {
                listener.Start(ListenBacklog);
            }
            catch (SocketException error)
            {
                Debug.LogWarning($"balls: control API cannot listen: {error.Message}");
                return;
            }
            Port = ((IPEndPoint)listener.LocalEndpoint).Port;
            running = true;
            thread.Start();
            Debug.Log($"balls: control API on http://127.0.0.1:{Port}/");
        }

        public void Dispose()
        {
            running = false;
            listener?.Stop();
            BallsNative.balls_control_destroy(control);
            control = IntPtr.Zero;
        }

        public void Pump()
        {
            while (TryDequeue(out Socket socket, out Request request))
            {
                Handle(request);
                Debug.Log($"balls: control {request.Path} -> {request.Status}");
                if (request.Answered)
                {
                    Respond(socket, request);
                }
                else
                {

                    captures.Add((Get(request.Query, "path") ?? string.Empty, request,
                        Time.realtimeSinceStartup + 10f));
                    pendingSockets[request] = socket;
                }
            }
            CompleteCaptures();
            if (quitRequested)
            {
                quitRequested = false;
                Dispose();
                Application.Quit();
            }
        }

        private readonly Dictionary<Request, Socket> pendingSockets = new();

        private void CompleteCaptures()
        {
            float now = Time.realtimeSinceStartup;
            for (int index = captures.Count - 1; index >= 0; --index)
            {
                (string path, Request request, float deadline) = captures[index];
                bool written = path.Length > 0 && File.Exists(path);
                if (!written && now < deadline)
                {
                    continue;
                }
                captures.RemoveAt(index);
                if (written)
                {
                    request.Status = 200;
                    request.Body = $"{{\"ok\":true,\"path\":{Json(path)}}}";
                }
                else
                {
                    request.Status = 500;
                    request.Body = "{\"ok\":false,\"error\":\"screenshot was not written\"}";
                }
                request.Answered = true;
                if (pendingSockets.Remove(request, out Socket socket))
                {
                    Respond(socket, request);
                }
            }
        }

        private void Handle(Request request)
        {
            if (HandleOwn(request))
            {
                return;
            }
            NativeControlContext context = Context();
            int status = BallsNative.balls_control_handle(control, ref context, request.Target, json, json.Length, out int length);
            if (length > json.Length)
            {
                json = new byte[length + 1];
                status = BallsNative.balls_control_handle(control, ref context, request.Target, json, json.Length, out length);
            }
            if (status > 0)
            {
                request.Status = status;
                request.Body = Encoding.UTF8.GetString(json, 0, length);
                request.Answered = true;
            }
            else if (status < 0)
            {
                Error(request, 404, "unknown path");
            }
            RunActions(request);
        }

        private byte[] json = new byte[65536];

        private bool HandleOwn(Request request)
        {
            string path = request.Path;
            Dictionary<string, string> query = request.Query;
            switch (path)
            {
                case "/import":
                {

                    var picked = new List<string>();
                    foreach (string entry in (Get(query, "path") ?? string.Empty).Split('|', StringSplitOptions.RemoveEmptyEntries))
                    {
                        picked.Add(entry);
                    }
                    if (picked.Count == 0)
                    {
                        Error(request, 400, "missing path");
                        return true;
                    }
                    app.FlowImport(picked);
                    string toast = BallsNative.FlowText(app.FlowHandle, BallsNative.FlowTextToast);
                    bool ok = app.FlowToastIsError == 0;
                    Ok(request, $"{{\"ok\":{(ok ? "true" : "false")},\"toast\":{Json(toast)}}}");
                    return true;
                }
                case "/compute-gpu":
                {
                    string wanted = Get(query, "id") ?? Get(query, "name") ?? "auto";
                    if (!BallsNative.SetComputeGpu(wanted))
                    {
                        Error(request, 400, "no compute GPU matches that id or name");
                        return true;
                    }
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
            }
            Screens.GameplayScreen gameplay = Gameplay();
            if (gameplay == null)
            {
                return false;
            }
            switch (path)
            {
                case "/start":
                    BallsInput.RequestAutoRelease();
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/launch":
                case "/launcher":
                    BallsInput.ClickLauncher(Integer(query, "column", 0), Integer(query, "row", 0));
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/rate":
                case "/limit":
                {
                    int index = Integer(query, "index", -1);
                    if (path == "/limit" && index < 0)
                    {
                        string mode = Get(query, "mode");
                        if (mode != null)
                        {
                            index = mode == "level" ? 0 : mode == "auto" ? 1 : mode == "max" ? 2 : -1;
                        }
                    }
                    if (index < 0)
                    {
                        Error(request, 400, path == "/limit" ? "index or mode is required" : "index is required");
                        return true;
                    }
                    if (path == "/rate")
                    {
                        gameplay.ControlSpawnRate(index);
                    }
                    else
                    {
                        gameplay.ControlBallLimit(index);
                    }
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
                case "/pause":
                case "/resume":
                {
                    bool wantPause = path == "/pause";
                    if (wantPause != app.IsPaused)
                    {
                        BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowTogglePause, -1);
                    }
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
                case "/restart":
                    BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowRestart, -1);
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/backend":
                {
                    string kind = Get(query, "kind") ?? string.Empty;
                    if (!BallsNative.TryBackendKind(kind, out int backendKind) || !BallsNative.BackendAvailable(backendKind))
                    {
                        Error(request, 409, "backend unknown or unavailable");
                        return true;
                    }
                    app.SwitchBackend(backendKind);
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
            }
            return false;
        }

        private NativeControlContext Context()
        {
            Screens.GameplayScreen gameplay = Gameplay();
            SessionSystem system = SessionSystem.Current;
            IntPtr session = gameplay != null && system?.Backend is Game.NativeGameBackend native ? native.SessionHandle : IntPtr.Zero;
            IntPtr benchmark = app.ScreenName == "benchmark" ? app.Get<Screens.BenchmarkScreen>().Handle : IntPtr.Zero;
            string screenName = BallsNative.FlowText(app.FlowHandle, BallsNative.FlowTextScreenName);
            if (string.IsNullOrEmpty(screenName))
            {
                screenName = "menu";
            }
            string toastText = BallsNative.FlowText(app.FlowHandle, BallsNative.FlowTextToast);
            string loadError = BallsNative.FlowText(app.FlowHandle, BallsNative.FlowTextLoadError);

            string fullScreenStr = UnityEngine.Screen.fullScreen ? "true" : "false";
            string orientationJson = Json(app.Settings?.OrientationName.ToLowerInvariant() ?? "auto");

            return new NativeControlContext
            {
                renderer = BallsApp.RendererText,
                screen = screenName,
                windowWidth = UnityEngine.Screen.width,
                windowHeight = UnityEngine.Screen.height,
                preferredBackend = app.DefaultBackendKind,
                vsync = QualitySettings.vSyncCount > 0 ? 1 : 0,
                toast = toastText,
                extraFields = "\"fullScreen\":" + fullScreenStr + ",\"orientation\":" + orientationJson,
                screenDetails = string.Empty,
                frames = system?.FrameTimingsHandle ?? IntPtr.Zero,
                settings = app.Settings?.ToNative() ?? new NativeSettings { defaultBackend = new byte[64] },
                session = session,
                mode = app.FlowMode,
                levelName = app.LevelName ?? string.Empty,
                loadError = loadError,
                diskState = app.FlowDiskState,
                paused = app.IsPaused ? 1 : 0,
                probeTargetFps = gameplay?.ProbeTargetFps ?? 60,
                benchmark = benchmark,
                profile = app.Profile.Handle,
            };
        }

        private void RunActions(Request request)
        {
            while (BallsNative.balls_control_take_action(control, out NativeControlAction action) != BallsNative.ControlNone)
            {
                switch (action.kind)
                {
                    case BallsNative.ControlScreenshot:
                        string capture = action.Text;
                        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(capture)) ?? ".");
                        ScreenCapture.CaptureScreenshot(capture);

                        request.Answered = false;
                        break;
                    case BallsNative.ControlPlay:
                        BallsNative.balls_flow_start_game(app.FlowHandle, action.mode, action.Text);
                        break;
                    case BallsNative.ControlMainMenu:
                        BallsNative.balls_flow_show_main_menu(app.FlowHandle);
                        break;
                    case BallsNative.ControlLevelSelect:
                        BallsNative.balls_flow_show_level_select(app.FlowHandle, action.mode);
                        break;
                    case BallsNative.ControlEditor:
                        BallsNative.balls_flow_open_editor(app.FlowHandle, action.mode, string.IsNullOrEmpty(action.Text) ? null : action.Text);
                        break;
                    case BallsNative.ControlQuit:

                        quitRequested = true;
                        BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowQuit, -1);
                        break;
                    case BallsNative.ControlApplySettings:
                        ApplySettings(action, request.Query);
                        break;
                    case BallsNative.ControlOpenSettings:
                        if (app.FlowScreen == BallsNative.ScreenGameplay)
                        {
                            if (!app.IsPaused)
                            {
                                BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowTogglePause, -1);
                            }
                            BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowPauseSettings, -1);
                        }
                        else
                        {
                            BallsNative.balls_flow_show_settings(app.FlowHandle);
                        }
                        break;
                    case BallsNative.ControlSetVsync:
                        if (app.Settings != null)
                        {
                            app.Settings.Vsync = action.value != 0;
                            app.SaveSettings();
                        }
                        QualitySettings.vSyncCount = action.value != 0 ? 1 : 0;
                        break;
                    case BallsNative.ControlRunBenchmark:
                        BallsNative.balls_flow_run_machine_benchmark(app.FlowHandle);
                        break;
                    case BallsNative.ControlSkipBenchmark:
                        BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowSkipBenchmark, -1);
                        break;
                    case BallsNative.ControlContinueBenchmark:
                        BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowContinueFromBenchmark, -1);
                        break;
                    case BallsNative.ControlSetPaused:
                        if ((action.value != 0) != app.IsPaused)
                        {
                            BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowTogglePause, -1);
                        }
                        break;
                    case BallsNative.ControlRestart:
                        BallsNative.balls_flow_activate(app.FlowHandle, BallsNative.FlowRestart, -1);
                        break;
                    case BallsNative.ControlStartProbe:
                        Gameplay()?.ControlProbe(action.value);
                        break;
                }
            }
        }

        private void ApplySettings(NativeControlAction action, Dictionary<string, string> query)
        {
            SettingsData settings = app.Settings;
            if (settings == null)
            {
                return;
            }
            NativeSettings next = action.settings;
            bool collisionsChanged = settings.BallCollisions != (next.ballCollisions != 0);
            settings.BallCollisions = next.ballCollisions != 0;
            settings.Orientation = next.orientation;
            settings.Vsync = next.vsync != 0;
            if (action.value >= 0)
            {
                settings.DefaultBackend = BallsNative.StoredBackendName(action.value);
            }
            app.SaveSettings();
            app.ApplyOrientation();
            QualitySettings.vSyncCount = settings.Vsync ? 1 : 0;
            if (collisionsChanged)
            {
                app.ApplyBallCollisions(settings.BallCollisions);
            }
        }

        private Screens.GameplayScreen Gameplay() => app.Get<Screens.GameplayScreen>();

        private void AcceptLoop()
        {
            while (running)
            {
                try
                {
                    Socket socket = listener.AcceptSocket();
                    ThreadPool.QueueUserWorkItem(_ => ReadRequest(socket));
                }
                catch (SocketException)
                {

                    return;
                }
                catch (ObjectDisposedException)
                {
                    return;
                }
            }
        }

        private void ReadRequest(Socket socket)
        {
            try
            {
                var buffer = new byte[8192];
                int read = socket.Receive(buffer);
                if (read <= 0)
                {
                    socket.Close();
                    return;
                }
                string text = Encoding.UTF8.GetString(buffer, 0, read);
                int endOfLine = text.IndexOf("\r\n", StringComparison.Ordinal);
                if (endOfLine < 0)
                {
                    socket.Close();
                    return;
                }
                string requestLine = text.Substring(0, endOfLine);
                string[] parts = requestLine.Split(' ');
                if (parts.Length < 2)
                {
                    socket.Close();
                    return;
                }
                string target = parts[1];
                string path = target;
                var query = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                int queryIndex = target.IndexOf('?');
                if (queryIndex >= 0)
                {
                    path = target.Substring(0, queryIndex);
                    string rawQuery = target.Substring(queryIndex + 1);
                    foreach (string pair in rawQuery.Split('&'))
                    {
                        if (pair.Length == 0)
                        {
                            continue;
                        }
                        int eq = pair.IndexOf('=');
                        string key = eq >= 0 ? pair.Substring(0, eq) : pair;
                        string value = eq >= 0 ? pair.Substring(eq + 1) : string.Empty;
                        query[Uri.UnescapeDataString(key)] = Uri.UnescapeDataString(value);
                    }
                }
                var request = new Request
                {
                    Path = path,
                    Target = target,
                    Query = query,
                };
                lock (queue)
                {
                    queue.Enqueue((socket, request));
                }
            }
            catch (Exception error)
            {
                Debug.LogWarning($"balls: error reading control request: {error.Message}");
                try
                {
                    socket.Close();
                }
                catch
                {

                }
            }
        }

        private bool TryDequeue(out Socket socket, out Request request)
        {
            lock (queue)
            {
                if (queue.Count > 0)
                {
                    (socket, request) = queue.Dequeue();
                    return true;
                }
            }
            socket = null;
            request = null;
            return false;
        }

        private static void Respond(Socket socket, Request request)
        {
            try
            {
                byte[] bytes = Encoding.UTF8.GetBytes(request.Body);
                string reason = request.Status switch
                {
                    200 => "OK",
                    400 => "Bad Request",
                    404 => "Not Found",
                    409 => "Conflict",
                    500 => "Internal Server Error",
                    _ => "Response",
                };
                string header = $"HTTP/1.1 {request.Status} {reason}\r\n" +
                                $"Content-Type: application/json; charset=utf-8\r\n" +
                                $"Content-Length: {bytes.Length}\r\n" +
                                "Connection: close\r\n\r\n";
                byte[] headerBytes = Encoding.UTF8.GetBytes(header);
                socket.Send(headerBytes);
                socket.Send(bytes);
                socket.Shutdown(SocketShutdown.Both);
                socket.Close();
            }
            catch (Exception error)
            {
                Debug.LogWarning($"balls: cannot send control reply: {error.Message}");
            }
        }

        private static void Ok(Request request, string body)
        {
            request.Status = 200;
            request.Body = body;
            request.Answered = true;
        }

        private static void Error(Request request, int status, string message)
        {
            request.Status = status;
            request.Body = $"{{\"ok\":false,\"error\":{Json(message)}}}";
            request.Answered = true;
        }

        private static string Get(Dictionary<string, string> query, string key) =>
            query.TryGetValue(key, out string value) ? value : null;

        private static int Integer(Dictionary<string, string> query, string key, int fallback) =>
            query.TryGetValue(key, out string value) && int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsed)
                ? parsed
                : fallback;

        private static string Json(string text)
        {
            if (text == null)
            {
                return "null";
            }
            var builder = new StringBuilder(text.Length + 4);
            builder.Append('"');
            foreach (char ch in text)
            {
                switch (ch)
                {
                    case '"': builder.Append("\\\""); break;
                    case '\\': builder.Append("\\\\"); break;
                    case '\b': builder.Append("\\b"); break;
                    case '\f': builder.Append("\\f"); break;
                    case '\n': builder.Append("\\n"); break;
                    case '\r': builder.Append("\\r"); break;
                    case '\t': builder.Append("\\t"); break;
                    default:
                        if (ch < ' ')
                        {
                            builder.AppendFormat("\\u{0:x4}", (int)ch);
                        }
                        else
                        {
                            builder.Append(ch);
                        }
                        break;
                }
            }
            builder.Append('"');
            return builder.ToString();
        }
    }
}
