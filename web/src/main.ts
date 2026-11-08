import './app.css';
import { importCredentialsFromLocation } from './js/auth.js';

// First, before any request: credentials from the device's QR link
// (http://<device>/#fbcred=...), removed from the address bar right away.
const imported = importCredentialsFromLocation();

// A phone that scanned the QR code gets a small "Device access" page (copy,
// save to the password manager) instead of the desktop IDE, which does not
// fit a phone screen; the IDE chunk is then not even downloaded from the
// device. Everything else loads the IDE.
const phone = typeof matchMedia === 'function' && matchMedia('(max-width: 767px)').matches;
if (imported && phone) {
  void import('./components/fb-access-page.js').then(() => {
    document.querySelector('fb-app')?.replaceWith(document.createElement('fb-access-page'));
    document.body.classList.add('ready');
  });
} else {
  void import('./ide.js');
}
