/*
 * HCore - the part of a Hatynka configuration page that is the same on every
 * device in the family.
 *
 * What is in here is everything that is not about what the device measures:
 * the transport, the link LED, the administrator session and its modal, the
 * factory-reset flow, the firmware line, the development device panel, the
 * translation lookup, and the one poll timer the page is allowed. An
 * application adds its own script beside this one, registers its strings and a
 * redraw hook, and gets all of the above for free.
 *
 * No framework, no build step and NO ES MODULES: a page opened as a file has an
 * opaque origin, and a browser refuses to load a module from one. A classic
 * script and a single global is the only shape that works both from file:// and
 * from the device.
 *
 * Where it talks to
 * -----------------
 * Served BY the device, every request is same-origin and there is nothing to
 * configure. Opened as a file during development there is no origin to speak of,
 * so the base URL comes from - in order - the ?device= query, what was typed
 * last (localStorage), or config.json beside the page. That last one is only
 * readable when the folder is served over http; from file:// the browser
 * refuses, which is exactly why the other two exist.
 *
 * The markup it expects
 * ---------------------
 * Every element below is optional: an application that has no factory-reset
 * panel simply does not draw one, and nothing here throws over it. See
 * README.md for the full list.
 */
(function () {
  'use strict';

  /* How long the reset modal counts for. The device needs a fraction of it -
     the number is chosen so somebody reading the screen believes it happened. */
  var RESET_WAIT_MS = 5000;

  var DEVICE_STORAGE_KEY = 'hatynka.device';
  var KEY_STORAGE_KEY = 'hatynka.key';

  var state = {
    base: '',           // '' means same-origin: the device served this page.
    key: null,          // Session key from POST /api/auth.
    auth: 'unknown',    // 'first' | 'unathorized' | 'ok' | 'unknown'
    link: 'wait',       // 'wait' | 'ok' | 'lost'
    lang: 'en',         // What the page is currently DRAWN in - see setLanguage.
    modalMode: 'signin',// 'signin' | 'first' | 'change'
    fw: null,           // Firmware version, from /api/info.
    tab: null           // Which tab is showing; null when the page has none.
  };

  /* What the application asked to be told about. All optional. */
  var hooks = {
    render: null,       // Redraw everything the app owns.
    status: null,       // A short sentence for wherever the app shows those.
    tab: null           // The showing tab changed.
  };

  var appStart = null;  // The app's boot, re-run when the dev panel changes device.
  var pollTask = null;
  var pollTab = null;   // Poll only while this tab shows; null means always.
  var pollMs = 0;
  var pollTimer = null;

  /* --------------------------------------------------------------- language --- */

  /*
   * Only the words the shared chrome shows. An application merges its own with
   * addStrings(), and may override any of these by using the same id.
   *
   * The device carries its own dictionary for the panel it drives itself;
   * duplicating a handful of labels here is cheaper than a request to fetch
   * them, and the two lists are short enough to keep in step by eye.
   */
  var TEXT = {
    en: {
      admin: 'Admin', anon: 'Unauthorized', checking: 'Checking…',
      signIn: 'Sign in', create: 'Create', cancel: 'Cancel',
      password: 'Password', newPassword: 'New password',
      askExisting: 'Enter the administrator password to change settings.',
      askFirst: 'No password has been set on this device yet. Choose one.',
      wrongPass: 'Wrong password.', emptyPass: 'Enter a password.',
      signOut: 'Signed out.', offline: 'Device did not answer.',
      linkOk: 'Connected', linkWait: 'Connecting…', linkLost: 'Connection lost',
      firmware: 'Firmware',
      fwTitle: 'Firmware update',
      fwText: 'Upload a .bin built for this device. It is checked before anything is written.',
      fwImage: 'Image', fwGo: 'Update', fwNoFile: 'Choose a file.',
      fwRunning: 'Running', fwSending: 'Uploading…', fwVerifying: 'Verifying…',
      fwDone: 'Updated. The device is restarting.',
      fwFailed: 'Update failed.', fwOffline: 'Device did not answer.',
      fwOlderAsk: 'That image is OLDER than what is running. Install it anyway?',
      fwInstallAnyway: 'Install anyway',
      // One per reason the device can answer with, so a refusal reads as a
      // sentence rather than as the wire's own vocabulary.
      'fwReason.not an image': 'That file is not a firmware image.',
      'fwReason.wrong chip': 'That image was built for a different chip.',
      'fwReason.no descriptor': 'That image has no version information.',
      'fwReason.wrong device': 'That image is for a different device.',
      'fwReason.bad version': 'That image has no usable version number.',
      'fwReason.older version': 'That image is older than what is running.',
      'fwReason.same version': 'That version is already running.',
      'fwReason.too large': 'That image is too large for this device.',
      'fwReason.write failed': 'The device could not write it to flash.',
      'fwReason.verify failed': 'The upload was incomplete or corrupted.',
      'fwReason.incomplete': 'The upload was incomplete.',
      resetTitle: 'Factory reset', resetGo: 'Reset',
      dangerText: 'Erases every setting and the administrator password, then restarts.',
      resetAsk: 'This erases every setting and the administrator password. It cannot be undone.',
      resetDoing: 'Erasing and restarting…', resetDone: 'Device reset.',
      close: 'Close', adminTitle: 'Administrator',
      changePass: 'Change password', changeTitle: 'Change password', changeGo: 'Change',
      askChange: 'Choose a new administrator password. You will be signed in with it.',
      passChanged: 'Password changed.', changeFailed: 'Could not change the password.'
    },
    uk: {
      admin: 'Адмін', anon: 'Не авторизовано', checking: 'Перевірка…',
      signIn: 'Увійти', create: 'Створити', cancel: 'Скасувати',
      password: 'Пароль', newPassword: 'Новий пароль',
      askExisting: 'Введіть пароль адміністратора, щоб змінювати налаштування.',
      askFirst: 'На цьому пристрої ще немає пароля. Створіть його.',
      wrongPass: 'Невірний пароль.', emptyPass: 'Введіть пароль.',
      signOut: 'Ви вийшли.', offline: 'Пристрій не відповідає.',
      linkOk: 'З’єднано', linkWait: 'З’єднання…', linkLost: 'Немає зв’язку',
      firmware: 'Прошивка',
      fwTitle: 'Оновлення прошивки',
      fwText: 'Завантажте .bin для цього пристрою. Файл перевіряється до запису.',
      fwImage: 'Файл', fwGo: 'Оновити', fwNoFile: 'Оберіть файл.',
      fwRunning: 'Встановлено', fwSending: 'Завантаження…', fwVerifying: 'Перевірка…',
      fwDone: 'Оновлено. Пристрій перезапускається.',
      fwFailed: 'Не вдалося оновити.', fwOffline: 'Пристрій не відповідає.',
      fwOlderAsk: 'Ця прошивка СТАРІША за встановлену. Все одно встановити?',
      fwInstallAnyway: 'Все одно встановити',
      'fwReason.not an image': 'Цей файл не є прошивкою.',
      'fwReason.wrong chip': 'Прошивку зібрано для іншого чипа.',
      'fwReason.no descriptor': 'У прошивці немає даних про версію.',
      'fwReason.wrong device': 'Ця прошивка для іншого пристрою.',
      'fwReason.bad version': 'У прошивки немає придатного номера версії.',
      'fwReason.older version': 'Ця прошивка старіша за встановлену.',
      'fwReason.same version': 'Ця версія вже встановлена.',
      'fwReason.too large': 'Прошивка завелика для цього пристрою.',
      'fwReason.write failed': 'Пристрій не зміг записати її у флеш.',
      'fwReason.verify failed': 'Завантаження неповне або пошкоджене.',
      'fwReason.incomplete': 'Завантаження неповне.',
      resetTitle: 'Скидання', resetGo: 'Скинути',
      dangerText: 'Стирає всі налаштування та пароль адміністратора, потім перезапускає.',
      resetAsk: 'Це зітре всі налаштування та пароль адміністратора. Дію не можна скасувати.',
      resetDoing: 'Стирання та перезапуск…', resetDone: 'Пристрій скинуто.',
      close: 'Закрити', adminTitle: 'Адміністратор',
      changePass: 'Змінити пароль', changeTitle: 'Зміна пароля', changeGo: 'Змінити',
      askChange: 'Оберіть новий пароль адміністратора. Ви увійдете з ним.',
      passChanged: 'Пароль змінено.', changeFailed: 'Не вдалося змінити пароль.'
    }
  };

  /**
   * Merges an application's dictionary in.
   *
   * A language the core does not know is created; one it does know is added to.
   * Later wins, so an application that wants "Reset" to read differently only
   * has to say so.
   */
  function addStrings(strings) {
    Object.keys(strings).forEach(function (lang) {
      if (!TEXT[lang]) { TEXT[lang] = {}; }
      Object.keys(strings[lang]).forEach(function (id) {
        TEXT[lang][id] = strings[lang][id];
      });
    });
  }

  function t(id) {
    var lang = TEXT[state.lang] ? state.lang : 'en';
    var word = TEXT[lang][id];
    return word === undefined ? (TEXT.en[id] === undefined ? id : TEXT.en[id]) : word;
  }

  /**
   * Sets the language the page is DRAWN in, and redraws.
   *
   * The application owns this because the application owns the draft: a select
   * that has been changed but not saved has to take effect immediately, and the
   * device is the authority on what is stored, not on what the person in front
   * of it is currently reading.
   */
  function setLanguage(lang) {
    if (state.lang === lang) { return; }
    state.lang = lang;
    render();
  }

  /* ---------------------------------------------------------------- helpers --- */

  function $(id) { return document.getElementById(id); }

  /*
   * Every DOM write below goes through one of these, and every one of them is
   * null-tolerant. Two reasons: tools/packui.py deletes the development panel
   * from the release build, and a sibling device may simply not have a factory
   * reset or a card grid. A shared layer that assumes markup is a shared layer
   * that only works in the app it was written for.
   */
  function show(element, visible) {
    if (element) { element.hidden = !visible; }
  }

  function setText(id, value) {
    var element = $(id);
    if (element) { element.textContent = value; }
  }

  function on(id, event, handler) {
    var element = $(id);
    if (element) { element.addEventListener(event, handler); }
  }

  function setDisabled(id, disabled) {
    var element = $(id);
    if (element) { element.disabled = disabled; }
  }

  /** True when this build still carries the development device panel. */
  function hasDevPanel() { return $('devPanel') !== null; }

  /** Marks an element as waiting: the shimmer stands in for the value. */
  function setLoading(element, loading) {
    if (element) { element.classList.toggle('skeleton', loading); }
  }

  /** A short sentence for the user, wherever the application chose to put those. */
  function status(text, clearAfterMs) {
    if (hooks.status) { hooks.status(text, clearAfterMs || 0); }
  }

  function isAdmin() { return state.auth === 'ok'; }

  /* -------------------------------------------------------------- transport --- */

  /**
   * One place for every request, so every one of them is timed out.
   *
   * A device that has gone away must not leave the page waiting forever - fetch
   * has no timeout of its own, and an access point can vanish mid-request when
   * somebody walks off with the thing.
   */
  function api(path, options) {
    options = options || {};

    var headers = { 'Accept': 'application/json' };
    if (options.body !== undefined) { headers['Content-Type'] = 'application/json'; }
    if (options.auth && state.key) { headers['Authentication-Info'] = state.key; }

    var controller = new AbortController();
    var timer = setTimeout(function () { controller.abort(); }, options.timeout || 6000);

    // Yellow only while there is doubt. A green light that blinked on every
    // poll would be noise; one that stays green until something actually fails
    // is information.
    if (state.link !== 'ok') { setLink('wait'); }

    return fetch(state.base + path, {
      method: options.method || 'GET',
      headers: headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      signal: controller.signal
    }).then(function (response) {
      clearTimeout(timer);

      // Any answer at all means the link is alive - a 401 is the device
      // talking, and colouring it red would blame the network for a password.
      setLink('ok');

      return response.json().catch(function () { return {}; }).then(function (data) {
        return { status: response.status, ok: response.ok, data: data };
      });
    }).catch(function (error) {
      clearTimeout(timer);
      setLink('lost');
      return { status: 0, ok: false, data: {}, error: error };
    });
  }

  /* ------------------------------------------------------------------ units --- */

  /*
   * A reading is a fact about the world, not about a screen preference, so the
   * API answers in one canonical unit and the conversion happens here, on the
   * edge that displays it - exactly as the device's own panel does it.
   *
   * Pure on purpose: they take the unit rather than reading it out of some
   * config, so an application can call them with a draft, with what is saved,
   * or with neither.
   */
  var units = {
    /** @param celsius what /api/values reports. */
    temperature: function (celsius, unit) {
      if (unit === 'fahrenheit') {
        return { value: celsius * 9 / 5 + 32, unit: '°F', decimals: 1 };
      }
      return { value: celsius, unit: '°C', decimals: 1 };
    },

    /** @param hectopascals what /api/values reports. */
    pressure: function (hectopascals, unit) {
      if (unit === 'mmHg') {
        // 133.322387415 Pa per mmHg, the definition rather than an approximation -
        // the same constant the firmware uses, for the same reason.
        return { value: hectopascals * 100 / 133.322387415, unit: 'mmHg', decimals: 0 };
      }
      return { value: hectopascals, unit: 'hPa', decimals: 0 };
    }
  };

  /* ---------------------------------------------------------------- link LED --- */

  /**
   * Green reachable, yellow trying, red gone.
   *
   * Driven by whether requests come back, not by anything the device says about
   * its own radio: from a browser those are the same question, and the useful
   * answer to "why is this number not changing" is whether the page can still
   * talk at all.
   */
  function setLink(newStatus) {
    if (state.link === newStatus) { return; }

    var wasLost = state.link === 'lost';
    state.link = newStatus;
    renderLink();

    if (newStatus === 'lost' && state.key) {
      // A key belongs to a session on a device this page can no longer reach,
      // and the most likely reasons the link dropped - the device rebooted,
      // slept, or was reset - are all reasons the key is already dead. Showing
      // "Admin" over a connection that does not exist invites edits that cannot
      // land, so the page drops back to unauthorised and says so.
      forgetKey();
      render();
    }

    // Back after an outage: ask what this browser is now, rather than assuming.
    // A device that came back factory-reset answers "first".
    if (newStatus === 'ok' && wasLost) {
      checkAuth();
    }
  }

  /** Applies the current link state. Also called when the language changes. */
  function renderLink() {
    var dot = $('linkDot');
    if (!dot) { return; }

    dot.classList.remove('led--ok', 'led--wait', 'led--lost');
    dot.classList.add('led--' + state.link);

    dot.title = state.link === 'ok' ? t('linkOk')
              : state.link === 'wait' ? t('linkWait')
              : t('linkLost');
    dot.setAttribute('aria-label', dot.title);
  }

  /* --------------------------------------------------------------- firmware --- */

  /** The version line at the foot of the page, in whatever language is current. */
  function renderFooter() {
    setText('fwVersion', state.fw ? (t('firmware') + ' v' + state.fw) : '');
  }

  /** Reads what firmware the device is running, for the footer. */
  function loadInfo() {
    return api('/api/info').then(function (result) {
      state.fw = (result.ok && result.data.fw) ? result.data.fw : null;
      renderFooter();
    });
  }

  /* ------------------------------------------------------------------- auth --- */

  function renderAuthChip() {
    var chip = $('authChip');
    if (!chip) { return; }

    chip.classList.remove('chip--admin', 'chip--anon', 'chip--busy');

    if (state.auth === 'ok') {
      chip.classList.add('chip--admin');
      setText('authChipText', t('admin'));
    } else if (state.auth === 'unknown') {
      chip.classList.add('chip--busy');
      setText('authChipText', t('checking'));
    } else {
      chip.classList.add('chip--anon');
      setText('authChipText', t('anon'));
    }
  }

  function rememberKey(key) {
    state.key = key;
    try { sessionStorage.setItem(KEY_STORAGE_KEY, key); } catch (e) { /* private mode */ }
  }

  function forgetKey() {
    state.key = null;
    state.auth = 'unathorized';
    try { sessionStorage.removeItem(KEY_STORAGE_KEY); } catch (e) { /* private mode */ }
  }

  /** Asks the device what this browser currently is, key included if we hold one. */
  function checkAuth() {
    state.auth = 'unknown';
    renderAuthChip();

    return api('/api/auth', { auth: true }).then(function (result) {
      state.auth = result.ok && result.data.status ? result.data.status : 'unathorized';

      // A key the device no longer honours is worse than no key: it makes every
      // guarded request fail in a way the UI cannot explain.
      if (state.auth !== 'ok' && state.key) { forgetKey(); }

      render();
    });
  }

  /**
   * @param mode 'signin' - a password exists and this browser needs a key.
   *             'first'  - no password exists yet; create one, then sign in.
   *             'change' - signed in, replacing the password with a new one.
   */
  function openModal(mode) {
    if (!$('modal')) { return; }

    state.modalMode = mode;

    $('passInput').value = '';
    show($('modalError'), false);

    setText('modalText', mode === 'first' ? t('askFirst')
                       : mode === 'change' ? t('askChange')
                       : t('askExisting'));

    render();
    show($('modal'), true);
    $('passInput').focus();
  }

  function closeModal() { show($('modal'), false); }

  function submitPassword() {
    var password = $('passInput').value;

    if (!password) {
      setText('modalError', t('emptyPass'));
      show($('modalError'), true);
      return;
    }

    var box = $('modal').querySelector('.modal__box');
    box.classList.add('busy');
    show($('modalError'), false);

    // Two of the three modes SET the password before signing in with it. The
    // first-run route needs no key precisely because there is nobody to
    // authorise against; the change route sends the key it currently holds.
    var setting = state.modalMode === 'first' || state.modalMode === 'change';
    var prepare = setting
      ? api('/api/setAdminPassword', {
          method: 'POST',
          body: { pass: password },
          auth: state.modalMode === 'change'
        })
      : Promise.resolve({ ok: true });

    prepare.then(function (result) {
      if (!result.ok) {
        box.classList.remove('busy');
        setText('modalError', result.status === 0 ? t('offline') : t('changeFailed'));
        show($('modalError'), true);
        return null;
      }

      // Changing the password invalidates every key the device had issued -
      // including the one just used to change it. Signing in again with the new
      // password is not politeness, it is the only way to still be admin a
      // moment from now.
      return api('/api/auth', { method: 'POST', body: { pass: password } });
    }).then(function (result) {
      if (!result) { return; }
      box.classList.remove('busy');

      if (!result.ok || !result.data.auth_key) {
        setText('modalError', result.status === 0 ? t('offline') : t('wrongPass'));
        show($('modalError'), true);
        return;
      }

      var changed = state.modalMode === 'change';

      rememberKey(result.data.auth_key);
      state.auth = 'ok';
      closeModal();
      render();

      if (changed) { status(t('passChanged'), 2500); }
    });
  }

  function onChipClick() {
    if (state.auth === 'ok') {
      // Signing out is local: the device's key lives until it expires or the
      // next login replaces it, and nothing here can or should reach into that.
      forgetKey();
      render();
      status(t('signOut'), 2000);
      return;
    }

    openModal(state.auth === 'first' ? 'first' : 'signin');
  }

  /* ---------------------------------------------------------- factory reset --- */

  function openResetModal() {
    if (!$('resetModal')) { return; }

    show($('resetProgress'), false);
    if ($('resetBar')) { $('resetBar').style.width = '0'; }

    // Back to the asking state: Cancel and Reset, no Close. Re-set every time
    // rather than assumed, so a second visit after a cancelled one is not left
    // wearing the previous visit's buttons.
    show($('resetCancel'), true);
    show($('resetConfirm'), true);
    show($('resetClose'), false);
    setDisabled('resetConfirm', false);
    setDisabled('resetClose', true);

    setText('resetText', t('resetAsk'));
    setText('resetTitle', t('resetTitle'));
    setText('resetCancel', t('cancel'));
    setText('resetConfirm', t('resetGo'));
    setText('resetClose', t('close'));

    show($('resetModal'), true);
  }

  function closeResetModal() { show($('resetModal'), false); }

  /**
   * Runs the bar for RESET_WAIT_MS and then stops for good.
   *
   * Nothing is polled while it runs and nothing is checked at the end: the
   * device erases its filesystem and reboots into Normal, where there is no
   * radio and no server. This connection is over, and pretending otherwise -
   * retrying, reconnecting, showing an error - would only produce a failure
   * message for something that worked.
   */
  function runResetProgress() {
    // Cancel and Reset go: neither means anything now - the device is already
    // erasing. Close takes their place immediately but stays DISABLED until the
    // count finishes, so the modal always has a button in it and never offers
    // one that would hide a job still running.
    show($('resetCancel'), false);
    show($('resetConfirm'), false);
    show($('resetClose'), true);
    setDisabled('resetClose', true);

    show($('resetProgress'), true);
    setText('resetText', t('resetDoing'));

    var started = Date.now();
    var timer = setInterval(function () {
      var done = Math.min(1, (Date.now() - started) / RESET_WAIT_MS);
      if ($('resetBar')) { $('resetBar').style.width = (done * 100) + '%'; }

      if (done >= 1) {
        clearInterval(timer);
        setText('resetText', t('resetDone'));
        show($('resetProgress'), false);
        setDisabled('resetClose', false);
      }
    }, 100);

    // The page stops asking, for good. Polling a device that is rebooting would
    // paint the link red and the values with dashes, on top of a message saying
    // all is well.
    stopPolling();
    pollTask = null;

    forgetKey();
    render();
  }

  function confirmReset() {
    setDisabled('resetConfirm', true);

    api('/api/factoryReset', { method: 'POST', body: {}, auth: true }).then(function (result) {
      setDisabled('resetConfirm', false);

      if (result.status === 401) {
        closeResetModal();
        forgetKey();
        checkAuth();
        return;
      }

      if (!result.ok) {
        setText('resetText', t('offline'));
        return;
      }

      runResetProgress();
    });
  }

  /* --------------------------------------------------------- firmware update --- */

  /*
   * The device checks the image before it writes a byte of it - chip, product,
   * version - so nothing here duplicates that. What this half owns is the two
   * things a browser can do and the device cannot: show how far the upload has
   * got, and ask a person whether they really meant to install something older.
   */

  var firmwareBusy = false;

  /** Turns the device's own reason into a sentence, or falls back to it. */
  function firmwareReason(reason) {
    if (!reason) { return t('fwFailed'); }
    var text = t('fwReason.' + reason);
    return text === 'fwReason.' + reason ? reason : text;
  }

  function setFirmwareProgress(fraction) {
    show($('firmwareProgress'), fraction !== null);
    if ($('firmwareBar') && fraction !== null) {
      $('firmwareBar').style.width = Math.round(fraction * 100) + '%';
    }
  }

  /**
   * POSTs the file as the raw request body.
   *
   * XMLHttpRequest rather than fetch, for one reason: fetch has no upload
   * progress at all. A megabyte over an access point takes long enough that a
   * page with no bar looks like a page that has hung.
   *
   * No timeout either, unlike api(): this request legitimately takes a minute,
   * and the device answers only once it has written and verified the image.
   */
  function sendFirmware(file, force) {
    return new Promise(function (resolve) {
      var request = new XMLHttpRequest();
      request.open('POST', state.base + '/api/ota' + (force ? '?force=1' : ''));
      request.setRequestHeader('Content-Type', 'application/octet-stream');
      if (state.key) { request.setRequestHeader('Authentication-Info', state.key); }

      request.upload.onprogress = function (event) {
        if (!event.lengthComputable) { return; }
        setFirmwareProgress(event.loaded / event.total);

        // The last byte is sent long before the device has finished verifying
        // and activating the image, so the bar reaching the end is not the end.
        if (event.loaded >= event.total) { setText('firmwareState', t('fwVerifying')); }
      };

      request.onload = function () {
        var data = {};
        try { data = JSON.parse(request.responseText); } catch (e) { data = {}; }
        setLink('ok');
        resolve({ status: request.status, ok: request.status >= 200 && request.status < 300,
                  data: data });
      };

      request.onerror = function () {
        setLink('lost');
        resolve({ status: 0, ok: false, data: {} });
      };

      request.send(file);
    });
  }

  function finishFirmware(result) {
    firmwareBusy = false;
    setDisabled('firmwareButton', false);

    if (result.ok) {
      setFirmwareProgress(null);
      setText('firmwareState', t('fwDone'));

      // The device is rebooting into Normal, where there is no portal to
      // answer. Polling it would paint the link red under a message saying all
      // is well - the same reason the factory reset stops asking.
      stopPolling();
      pollTask = null;
      setDisabled('firmwareButton', true);
      forgetKey();
      render();
      return;
    }

    setFirmwareProgress(null);

    if (result.status === 401) {
      forgetKey();
      checkAuth();
      setText('firmwareState', t('anon'));
      return;
    }

    if (result.status === 0) {
      setText('firmwareState', t('fwOffline'));
      return;
    }

    setText('firmwareState', firmwareReason(result.data.reason));
  }

  function uploadFirmware() {
    if (firmwareBusy) { return; }

    var input = $('firmwareFile');
    var file = input && input.files && input.files[0];
    if (!file) {
      setText('firmwareState', t('fwNoFile'));
      return;
    }

    firmwareBusy = true;
    setDisabled('firmwareButton', true);
    setText('firmwareState', t('fwSending'));
    setFirmwareProgress(0);

    sendFirmware(file, false).then(function (result) {
      // A downgrade is refused by default and allowed on purpose. Asking here
      // rather than sending force=1 from the start is what keeps "older" an
      // answer somebody has to agree with instead of one nobody ever sees.
      if (!result.ok && result.data && result.data.reason === 'older version') {
        var offered = result.data.offered || '?';
        var current = result.data.current || '?';

        if (window.confirm(t('fwOlderAsk') + '\n\n' + offered + ' ← ' + current)) {
          setText('firmwareState', t('fwSending'));
          setFirmwareProgress(0);
          sendFirmware(file, true).then(finishFirmware);
          return;
        }
      }

      finishFirmware(result);
    });
  }

  /** The running version and whether the button may be pressed. */
  function renderFirmwarePanel() {
    setText('firmwareTitle', t('fwTitle'));
    setText('firmwareText', t('fwText'));
    setText('firmwareFileLabel', t('fwImage'));
    setText('firmwareButton', t('fwGo'));

    // What is being upgraded FROM. The device refuses a downgrade on its own,
    // but somebody choosing a file should not have to guess what they are
    // replacing - and this is already in hand from GET /api/info.
    setText('firmwareRunning', state.fw ? t('fwRunning') + ': ' + state.fw : '');

    // Admin only, and never while one is already on its way.
    setDisabled('firmwareButton', firmwareBusy || !isAdmin());
  }

  /* ------------------------------------------------------------ where to talk --- */

  /**
   * Resolves the device's base URL, and shows the development panel only when
   * this page was NOT served by a device.
   */
  function resolveBase() {
    // Served over http(s) by something: that something is the device, unless a
    // ?device= override says to talk to a different one.
    if (location.protocol === 'http:' || location.protocol === 'https:') {
      var fromQuery = new URLSearchParams(location.search).get('device');
      if (fromQuery && hasDevPanel()) {
        state.base = fromQuery.replace(/\/$/, '');
        show($('devPanel'), true);
        $('deviceInput').value = state.base;
        return Promise.resolve();
      }

      // Same origin. No panel, nothing to configure.
      state.base = '';
      return Promise.resolve();
    }

    // Opened as a file. Without the development panel there is nothing this
    // build can do about that, which is the right answer for a release.
    if (!hasDevPanel()) {
      state.base = '';
      return Promise.resolve();
    }

    // Remembered address first, then config.json - which the browser refuses to
    // read from file://, and which is exactly why the field exists.
    show($('devPanel'), true);

    var stored = null;
    try { stored = localStorage.getItem(DEVICE_STORAGE_KEY); } catch (e) { /* private mode */ }

    if (stored) {
      state.base = stored;
      $('deviceInput').value = stored;
      return Promise.resolve();
    }

    return fetch('config.json').then(function (response) {
      return response.json();
    }).then(function (config) {
      state.base = (config.device || '').replace(/\/$/, '');
      $('deviceInput').value = state.base;
    }).catch(function () {
      setText('deviceState', 'Enter the address shown on the device screen.');
    });
  }

  function onDeviceConnect() {
    var value = $('deviceInput').value.trim().replace(/\/$/, '');
    if (!value) { return; }

    state.base = value;
    try { localStorage.setItem(DEVICE_STORAGE_KEY, value); } catch (e) { /* private mode */ }

    setText('deviceState', '');
    forgetKey();
    if (appStart) { appStart(); }
  }

  /* ------------------------------------------------------------------ polling --- */

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  /**
   * Is there any reason to be asking the device anything right now?
   *
   * Three ways for the answer to be no, and they are the same answer: nobody is
   * looking. The tab is hidden, or the page is showing a different one from the
   * tab the readings are on. A device serving this page is an access point
   * running off a battery, and a request nobody will read is not free.
   */
  function pollWanted() {
    if (!pollTask) { return false; }
    if (document.hidden) { return false; }
    if (pollTab && state.tab && pollTab !== state.tab) { return false; }
    return true;
  }

  /**
   * Starts the timer if it should be running and is not.
   *
   * Fires once immediately, because this is only ever called on the way BACK -
   * a tab shown again, a window uncovered - and the first thing somebody
   * returning wants is a fresh number, not one up to ten seconds old.
   */
  function resumePolling() {
    if (pollTimer || !pollWanted()) { return; }

    pollTask();
    pollTimer = setInterval(pollTask, pollMs);
  }

  /** Whichever of the two the current state calls for. */
  function applyPolling() {
    if (pollWanted()) { resumePolling(); } else { stopPolling(); }
  }

  /**
   * The page's one recurring request.
   *
   * One, not many: a device serving this page is an access point running off a
   * battery, and every timer an application forgets to cancel is paid for in
   * milliamps. Calling this again replaces whatever was running.
   *
   * @param tabId optional - poll only while that tab is the one showing.
   *              Readings on a dashboard have no reason to be fetched while
   *              somebody is reading the settings.
   *
   * Deliberately does NOT fire immediately: the application's own boot decides
   * when the first request goes out, and firing here as well would double it.
   */
  function poll(task, intervalMs, tabId) {
    stopPolling();

    pollTask = task;
    pollMs = intervalMs;
    pollTab = tabId || null;

    if (pollWanted()) { pollTimer = setInterval(task, intervalMs); }
  }

  /* --------------------------------------------------------------------- tabs --- */

  /*
   * Tabs are declared in markup, not registered in code.
   *
   *   <button class="tab" data-tab="settings" data-text="tabSettings">Settings</button>
   *   <section data-pane="settings"> ... </section>
   *
   * `data-tab` names it, `data-pane` is what that name shows, and `data-text`
   * is the dictionary id its label is drawn from - so the strip changes language
   * with everything else. More than one pane may carry the same `data-pane`,
   * which is how a tab holds several panels without a wrapper around them.
   *
   * A page with no `.tab` buttons has no tabs and nothing here does anything.
   * That is not a special case in the code, it just falls out: there is nothing
   * to hide, because hiding is driven by the buttons that exist.
   */

  function tabButtons() {
    return Array.prototype.slice.call(document.querySelectorAll('.tab[data-tab]'));
  }

  function tabPanes() {
    return Array.prototype.slice.call(document.querySelectorAll('[data-pane]'));
  }

  /** The names in strip order, so "the first tab" has a meaning. */
  function tabNames() {
    return tabButtons().map(function (button) { return button.getAttribute('data-tab'); });
  }

  /**
   * Shows one tab and tells the application.
   *
   * An unknown name falls back to the first rather than showing nothing: it
   * arrives from the URL hash, which anybody can type and a stale bookmark can
   * outlive a renamed tab.
   */
  function showTab(name) {
    var names = tabNames();
    if (!names.length) { return; }

    if (names.indexOf(name) === -1) { name = names[0]; }
    if (state.tab === name) { return; }

    state.tab = name;
    renderTabs();
    rememberTab(name);

    // A tab nobody is looking at has no reason to be polled - see pollWanted().
    applyPolling();

    if (hooks.tab) { hooks.tab(name); }
  }

  /** Applies the current tab to the strip and the panes. */
  function renderTabs() {
    var buttons = tabButtons();
    if (!buttons.length) { return; }

    buttons.forEach(function (button) {
      var on = button.getAttribute('data-tab') === state.tab;
      var id = button.getAttribute('data-text');

      button.classList.toggle('tab--on', on);
      button.setAttribute('aria-selected', on ? 'true' : 'false');

      // Roving tabindex: the strip is ONE stop for the Tab key, and the arrow
      // keys move within it. A row of five buttons that each need a press of
      // Tab to walk past is a row that gets walked past.
      button.tabIndex = on ? 0 : -1;

      if (id) { button.textContent = t(id); }
    });

    tabPanes().forEach(function (pane) {
      pane.hidden = pane.getAttribute('data-pane') !== state.tab;
    });
  }

  /*
   * The showing tab lives in the URL hash.
   *
   * It survives a reload, it can be linked to - "open #settings and look at the
   * firmware panel" is a sentence somebody can act on - and it costs no storage
   * on a device that has little. replaceState rather than assignment, so a
   * morning of switching tabs does not bury the Back button.
   */
  function tabFromHash() {
    return (location.hash || '').replace(/^#/, '') || null;
  }

  function rememberTab(name) {
    if (tabFromHash() === name) { return; }

    try {
      history.replaceState(null, '', '#' + name);
    } catch (e) {
      location.hash = name;   // no history API, or a file:// page that refuses
    }
  }

  /** Left and right walk the strip; Home and End jump to its ends. */
  function onTabKey(event) {
    var names = tabNames();
    var at = names.indexOf(state.tab);
    if (at === -1) { return; }

    // null, not -1, for "some other key": ArrowLeft on the first tab computes
    // -1 legitimately, and a sentinel that collides with a real answer is a
    // wrap that silently does nothing.
    var to = event.key === 'ArrowRight' ? at + 1
           : event.key === 'ArrowLeft' ? at - 1
           : event.key === 'Home' ? 0
           : event.key === 'End' ? names.length - 1
           : null;

    if (to === null) { return; }

    event.preventDefault();
    showTab(names[(to + names.length) % names.length]);   // wraps, both ways

    // Moving the focus with it, or the next arrow press starts over from a
    // button that is no longer the one showing.
    var moved = tabButtons()[names.indexOf(state.tab)];
    if (moved && moved.focus) { moved.focus(); }
  }

  function wireTabs() {
    var buttons = tabButtons();
    if (!buttons.length) { return; }

    buttons.forEach(function (button) {
      button.addEventListener('click', function () {
        showTab(button.getAttribute('data-tab'));
      });
      button.addEventListener('keydown', onTabKey);
    });

    // Somebody edited the hash, or used Back onto a page that had one.
    window.addEventListener('hashchange', function () {
      showTab(tabFromHash() || tabNames()[0]);
    });

    // state.tab is null here, so this always lands somewhere: the hash if it
    // names a tab, the first one if it does not.
    state.tab = null;
    showTab(tabFromHash());
  }

  /* ------------------------------------------------------------------- draw --- */

  /** Everything the shared chrome owns, in whatever language is current. */
  function renderShell() {
    renderTabs();
    renderAuthChip();
    renderLink();
    renderFooter();

    var newPassword = state.modalMode === 'first' || state.modalMode === 'change';
    setText('modalTitle', state.modalMode === 'change' ? t('changeTitle') : t('adminTitle'));
    setText('passLabel', newPassword ? t('newPassword') : t('password'));
    setText('modalCancel', t('cancel'));
    setText('modalSubmit', state.modalMode === 'first' ? t('create')
                         : state.modalMode === 'change' ? t('changeGo')
                         : t('signIn'));

    setText('changePassButton', t('changePass'));
    renderFirmwarePanel();
    setText('dangerTitle', t('resetTitle'));
    setText('dangerText', t('dangerText'));
    setText('resetButton', t('resetGo'));
  }

  /** Redraws the chrome, then whatever the application put on top of it. */
  function render() {
    renderShell();
    if (hooks.render) { hooks.render(); }
  }

  /* ------------------------------------------------------------------- boot --- */

  function wire() {
    wireTabs();

    on('authChip', 'click', onChipClick);
    on('modalCancel', 'click', closeModal);
    on('modalSubmit', 'click', submitPassword);
    on('changePassButton', 'click', function () { openModal('change'); });

    on('firmwareButton', 'click', uploadFirmware);

    // Clears a stale refusal the moment a different file is chosen, so the
    // message on screen always belongs to the file in the box.
    on('firmwareFile', 'change', function () { setText('firmwareState', ''); });

    on('resetButton', 'click', openResetModal);
    on('resetCancel', 'click', closeResetModal);
    on('resetConfirm', 'click', confirmReset);
    on('resetClose', 'click', closeResetModal);

    on('deviceButton', 'click', onDeviceConnect);

    on('passInput', 'keydown', function (event) {
      if (event.key === 'Enter') { submitPassword(); }
    });

    on('modal', 'click', function (event) {
      if (event.target === $('modal')) { closeModal(); }
    });

    // Polling a device nobody is looking at is a request every few seconds for
    // nothing - and on a battery-powered access point, that is not free. The
    // showing tab is the other half of that question; both live in pollWanted().
    document.addEventListener('visibilitychange', applyPolling);
  }

  /**
   * Hands control back to the application once there is a device to talk to.
   *
   * `start` is kept rather than called and forgotten: pointing the development
   * panel at a different device has to re-run the whole boot, and only the
   * application knows what its boot is.
   */
  function begin(start) {
    appStart = start;

    wire();

    try { state.key = sessionStorage.getItem(KEY_STORAGE_KEY); } catch (e) { state.key = null; }

    return resolveBase().then(function () { appStart(); });
  }

  /* ---------------------------------------------------------------- exports --- */

  window.HCore = {
    state: state,

    // DOM
    $: $,
    show: show,
    setText: setText,
    on: on,
    setDisabled: setDisabled,
    setLoading: setLoading,
    hasDevPanel: hasDevPanel,

    // Words
    t: t,
    addStrings: addStrings,
    setLanguage: setLanguage,

    // Device
    api: api,
    units: units,
    loadInfo: loadInfo,
    checkAuth: checkAuth,
    isAdmin: isAdmin,
    openPasswordChange: function () { openModal('change'); },

    // Page
    poll: poll,
    render: render,
    begin: begin,

    /** Show a tab by its `data-tab` name. */
    showTab: showTab,

    /** Which tab is showing, or null on a page that has none. */
    tab: function () { return state.tab; },

    /** Redraw whatever the application owns; called after every core change. */
    onRender: function (callback) { hooks.render = callback; },

    /** Where the application shows a short sentence: "Signed out.", and such. */
    onStatus: function (callback) { hooks.status = callback; },

    /** The showing tab changed. Passed the new one's name. */
    onTab: function (callback) { hooks.tab = callback; }
  };
})();
