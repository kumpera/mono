using System;
using System.IO;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Newtonsoft.Json.Linq;

namespace WsProxy
{

	public class Startup {
		private readonly IConfiguration configuration;
		public Startup (IConfiguration configuration)
		{
			this.configuration = configuration;
		}

		// This method gets called by the runtime. Use this method to add services to the container.
		// For more information on how to configure your application, visit https://go.microsoft.com/fwlink/?LinkID=398940
		public void ConfigureServices (IServiceCollection services)
		{
			services.AddRouting ();
		}

		public static async Task ProxyMsg (string desc, WebSocket from, WebSocket to)
		{
			byte [] buff = new byte [4000];
			var mem = new MemoryStream ();
			while (true) {
				var result = await from.ReceiveAsync (new ArraySegment<byte> (buff), CancellationToken.None);
				if (result.MessageType == WebSocketMessageType.Close) {
					await to.SendAsync (new ArraySegment<byte> (mem.GetBuffer (), 0, (int)mem.Length), WebSocketMessageType.Close, true, CancellationToken.None);
					return;
				}

				if (result.EndOfMessage) {
					mem.Write (buff, 0, result.Count);

					var str = Encoding.UTF8.GetString (mem.GetBuffer (), 0, (int)mem.Length);

					await to.SendAsync (new ArraySegment<byte> (mem.GetBuffer (), 0, (int)mem.Length), WebSocketMessageType.Text, true, CancellationToken.None);
					mem.SetLength (0);
				} else {
					mem.Write (buff, 0, result.Count);
				}
			}
		}

		Uri GetBrowserUri (string path)
		{
			return new Uri ("ws://localhost:9222" + path);
		}

		async Task SendNodeVersion (HttpContext context) {
			Console.WriteLine ("hello chrome! json/version");
			var resp_obj = new JObject();
			resp_obj["Browser"] = "node.js/v9.11.1";
			resp_obj["Protocol-Version"] = "1.1";

			var response = resp_obj.ToString ();
			await context.Response.WriteAsync (response, new CancellationTokenSource ().Token);
		}

		async Task SendNodeList (HttpContext context) {
			Console.WriteLine ("hello chrome! json/list");
			try {
			var response = new JArray (JObject.FromObject (new {
				description = "node.js instance",
				devtoolsFrontendUrl = "chrome-devtools://devtools/bundled/inspector.html?experiments=true&v8only=true&ws=localhost:9300/91d87807-8a81-4f49-878c-a5604103b0a4",
				faviconUrl = "https://nodejs.org/static/favicon.ico",
				id =  "91d87807-8a81-4f49-878c-a5604103b0a4",
				title = "foo.js",
				type = "node",
				url = "file:///Users/kumpera/src/mono/sdks/wasm/dbg/foo.js",
				webSocketDebuggerUrl = "ws://localhost:9300/91d87807-8a81-4f49-878c-a5604103b0a4"
			})).ToString ();

			Console.WriteLine ($"sending: {response}");
			await context.Response.WriteAsync (response, new CancellationTokenSource ().Token);
			} catch (Exception e) {Console.WriteLine(e);}
		}

		// This method gets called by the runtime. Use this method to configure the HTTP request pipeline.
		public void Configure (IApplicationBuilder app, IHostingEnvironment env)
		{
			app.UseDeveloperExceptionPage ();

			app.UseWebSockets ();
			
			app.UseRouter (router => {
				router.MapGet ("devtools/page/{pageId}", async context => {
					if (!context.WebSockets.IsWebSocketRequest) {
						context.Response.StatusCode = 400;
						return;
					}

					try {
						var proxy = new MonoProxy ();
						var browserUri = GetBrowserUri (context.Request.Path.ToString ());
						var ideSocket = await context.WebSockets.AcceptWebSocketAsync ();

						await proxy.Run (browserUri, ideSocket);
					} catch (Exception e) {
						Console.WriteLine ("got exception {0}", e);
					}
				});
			});

			if (configuration ["NodeApp"] != null) {
				var nodeApp = configuration ["NodeApp"];
				Console.WriteLine($"Doing the nodejs: {nodeApp}");
				Console.WriteLine (Path.GetFullPath (nodeApp));
				app.UseRouter (router => {
					//Inspector API for using chrome devtools directly
					router.MapGet ("json", SendNodeList);
					router.MapGet ("json/list", SendNodeList);
					router.MapGet ("json/version", SendNodeVersion);
					router.MapGet ("launch-and-connect", async context => {
						if (!context.WebSockets.IsWebSocketRequest) {
							context.Response.StatusCode = 400;
							return;
						}

						var nodeFullPath = Path.GetFullPath (nodeApp);
						var psi = new ProcessStartInfo ();
						psi.Arguments = $"--inspect-brk=localhost:0 {nodeFullPath}";
						psi.UseShellExecute = false;
						psi.FileName = "node";
						psi.WorkingDirectory = Path.GetDirectoryName (nodeFullPath);
						psi.RedirectStandardError = true;
						psi.RedirectStandardOutput = true;

						var tcs = new TaskCompletionSource<string> ();

						var proc = Process.Start (psi);
						try {
							proc.ErrorDataReceived += (sender, e) => {
								Console.WriteLine ($"stderr: {e.Data}");
								if (e.Data.StartsWith ("Debugger listening on ", StringComparison.Ordinal)) {
									tcs.TrySetResult (e.Data.Substring (e.Data.IndexOf ("ws://", StringComparison.Ordinal)));
								}
							};
							proc.OutputDataReceived += (sender, e) => {
								Console.WriteLine ($"stdout: {e.Data}");
							};
							proc.BeginErrorReadLine ();
							proc.BeginOutputReadLine ();

							if (await Task.WhenAny (tcs.Task, Task.Delay (2000)) != tcs.Task) {
								Console.WriteLine ("Didnt get the con string after 2s.");
								throw new Exception ("node.js timedout");
							}
							var con_str = await tcs.Task;
							Console.WriteLine ($"lauching proxy for {con_str}");

							try {
								var proxy = new MonoProxy ();
								var browserUri = new Uri (con_str);
								var ideSocket = await context.WebSockets.AcceptWebSocketAsync ();

								await proxy.Run (browserUri, ideSocket);
								Console.WriteLine("Proxy done");
							} catch (Exception e) {
								Console.WriteLine ("got exception {0}", e.GetType().FullName);
							}
						} finally {
							Console.WriteLine ("DONE");
							proc.CancelErrorRead ();
							proc.CancelOutputRead ();
							proc.Kill ();
							proc.WaitForExit ();
							proc.Close ();
						}
					});
				});
			}
		}
	}
}
