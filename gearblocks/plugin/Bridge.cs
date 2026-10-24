using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Text;
using System.Threading;

namespace BeamMP.GearBlocks;

// Loopback-only line transport between a Lua script and the launcher. The script never chooses the
// address: the host is always 127.0.0.1 and the port (and token) come from the launcher's environment.
// Everything here is plain .NET and runs on its own threads; it never touches game objects.
internal sealed class Bridge : IDisposable
{
    public const int MaxLineBytes = 1024 * 1024;
    public const int MaxQueuedLines = 4096;

    readonly int port;
    readonly string token;
    readonly ConcurrentQueue<string> inbox = new();
    readonly ConcurrentQueue<string> dataInbox = new();
    readonly ConcurrentQueue<string> outbox = new();
    readonly AutoResetEvent wake = new(false);
    volatile bool connected;
    volatile bool stop;
    string lastError = "not started";
    Thread worker;

    public Bridge(int port, string token)
    {
        this.port = port;
        this.token = token;
    }

    public bool Connected => connected;

    /// Connecting or connected: false once the connection has ended or failed.
    public bool Active => worker != null && worker.IsAlive;
    public string LastError => lastError;

    public void Start()
    {
        if (worker != null) return;
        worker = new Thread(Run) { IsBackground = true, Name = "BeamMP-bridge" };
        worker.Start();
    }

    public bool Send(string line)
    {
        if (!connected || stop || line.Length == 0 || line.Contains('\n') || Encoding.UTF8.GetByteCount(line) > MaxLineBytes) return false;
        if (outbox.Count >= MaxQueuedLines) return false;
        outbox.Enqueue(line);
        wake.Set();
        return true;
    }

    // Lines for the Lua script.
    public string Poll() => inbox.TryDequeue(out var line) ? line : null;

    // Game data from the other players (`{"type":"data",...}`), for the plugin itself: the script never sees these.
    public bool PollData(out string line) => dataInbox.TryDequeue(out line);

    // Session messages also tell the plugin where the player stands in the room; the script still gets them.
    void Route(string line)
    {
        string type = null;
        JsonDocument document = null;
        try
        {
            document = JsonDocument.Parse(line);
            if (document.RootElement.ValueKind == JsonValueKind.Object && document.RootElement.TryGetProperty("type", out var t)) type = t.GetString();
        }
        catch (JsonException) { }
        try
        {
            if (type == "data")
            {
                if (dataInbox.Count < MaxQueuedLines) dataInbox.Enqueue(line);
                return;
            }
            if (type == "session" && document.RootElement.TryGetProperty("session", out var session)) Room.Set(RoomState.Parse(session));
        }
        finally
        {
            document?.Dispose();
        }
        if (inbox.Count < MaxQueuedLines) inbox.Enqueue(line);
    }

    void Run()
    {
        try
        {
            using var client = new TcpClient(AddressFamily.InterNetwork) { NoDelay = true };
            var connect = client.ConnectAsync(IPAddress.Loopback, port);
            if (!connect.Wait(3000)) throw new TimeoutException("connect timed out");
            using var stream = client.GetStream();
            var writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
            writer.WriteLine(token);
            connected = true;
            lastError = "";
            var reader = new Thread(() => ReadLoop(stream)) { IsBackground = true, Name = "BeamMP-bridge-read" };
            reader.Start();
            while (!stop && connected)
            {
                wake.WaitOne(250);
                while (outbox.TryDequeue(out var line)) writer.WriteLine(line);
            }
        }
        catch (Exception e)
        {
            lastError = e.Message;
        }
        finally
        {
            connected = false;
            Room.Set(RoomState.None); // the launcher is gone: no room to speak of
        }
    }

    void ReadLoop(NetworkStream stream)
    {
        var buffer = new byte[8192];
        var line = new MemoryStream();
        try
        {
            int n;
            while (!stop && (n = stream.Read(buffer, 0, buffer.Length)) > 0)
            {
                for (int i = 0; i < n; i++)
                {
                    if (buffer[i] == (byte)'\n')
                    {
                        Route(Encoding.UTF8.GetString(line.GetBuffer(), 0, (int)line.Length));
                        line.SetLength(0);
                    }
                    else if (line.Length < MaxLineBytes)
                    {
                        line.WriteByte(buffer[i]);
                    }
                    else
                    {
                        throw new InvalidDataException("line too long");
                    }
                }
            }
        }
        catch (Exception e)
        {
            lastError = e.Message;
        }
        connected = false;
        wake.Set();
    }

    public void Dispose()
    {
        stop = true;
        connected = false;
        wake.Set();
    }
}
