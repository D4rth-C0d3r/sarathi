package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.lifecycle.lifecycleScope
import io.github.d4rthc0d3r.sarathi.MainActivity
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import kotlinx.coroutines.launch

/** Where OSM's login comes back (ADR-0012 §5): finishes it, then returns to the app. */
class OsmLoginActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handle(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handle(intent)
    }

    private fun handle(intent: Intent) {
        val redirect = intent.data ?: return finish()
        lifecycleScope.launch {
            val ok =
                OsmAccount.finishLogin(
                    this@OsmLoginActivity,
                    MapsRegistry.get(this@OsmLoginActivity).http,
                    redirect,
                )
            Toast
                .makeText(
                    this@OsmLoginActivity,
                    if (ok) R.string.osm_signed_in else R.string.osm_not_signed_in,
                    Toast.LENGTH_LONG,
                ).show()
            startActivity(
                Intent(this@OsmLoginActivity, MainActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP),
            )
            finish()
        }
    }
}
