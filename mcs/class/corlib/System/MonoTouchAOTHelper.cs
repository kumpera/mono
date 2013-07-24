using System;

#if MONOTOUCH
using Crimson.CommonCrypto;

namespace System {
	internal class MonoTouchAOTHelper {
		internal static bool FalseFlag = false;
	}

	internal class FullAOTHelper

		static FullAOTHelper () {
			if (FalseFlag) {
				var comparer = new System.Collections.Generic.GenericComparer <Guid> ();
				var eqcomparer = new System.Collections.Generic.GenericEqualityComparer <Guid> ();
			}
		}

		internal static void GetRandom (byte[] b)
		{
			Cryptor.GetRandom (b);
		}
	}
}
#endif
