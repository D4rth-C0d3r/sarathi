package io.github.d4rthc0d3r.sarathi

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import io.github.d4rthc0d3r.sarathi.ui.PlaceholderScreen
import io.github.d4rthc0d3r.sarathi.ui.theme.SarathiTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            SarathiTheme {
                PlaceholderScreen()
            }
        }
    }
}
