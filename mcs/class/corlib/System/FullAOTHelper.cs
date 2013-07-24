//
// System.FulAotHelper.cs
//
// Authors:
//	Rodrigo Kumpera
//
// Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
using System.Security.Cryptography;

namespace System
{
	internal class FullAOTHelper
	{
		internal static bool FalseFlag = false;

		private static object _rngAccess = new object ();
		private static RandomNumberGenerator _rng;
		private static RandomNumberGenerator _fastRng;

		//FIXME do we need it?
		static FullAOTHelper () {
			if (FalseFlag) {
				var comparer = new System.Collections.Generic.GenericComparer <Guid> ();
				var eqcomparer = new System.Collections.Generic.GenericEqualityComparer <Guid> ();
			}
		}

		internal static void GetRandom (byte[] b)
		{
			// thread-safe access to the prng
			lock (_rngAccess) {
				if (_rng == null)
					_rng = RandomNumberGenerator.Create ();
				_rng.GetBytes (b);
			}	
		}
	}
}
