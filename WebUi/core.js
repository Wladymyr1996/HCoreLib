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
    fw: null            // Firmware version, from /api/info.
  };

  /* What the application asked to be told about. All optional. */
  var hooks = {
    render: null,       // Redraw everything the app owns.
    status: null        // A short sentence for wherever the app shows those.
  };

  var appStart = null;  // The app's boot, re-run when the dev panel changes device.
  var pollTask = null;
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
   * The page's one recurring request.
   *
   * One, not many: a device serving this page is an access point running off a
   * battery, and every timer an application forgets to cancel is paid for in
   * milliamps. Calling this again replaces whatever was running.
   */
  function poll(task, intervalMs) {
    stopPolling();
    pollTask = task;
    pollMs = intervalMs;
    pollTimer = setInterval(task, intervalMs);
  }

  /* ------------------------------------------------------------------- draw --- */

  /** Everything the shared chrome owns, in whatever language is current. */
  function renderShell() {
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
    on('authChip', 'click', onChipClick);
    on('modalCancel', 'click', closeModal);
    on('modalSubmit', 'click', submitPassword);
    on('changePassButton', 'click', function () { openModal('change'); });

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
    // nothing - and on a battery-powered access point, that is not free.
    document.addEventListener('visibilitychange', function () {
      if (!pollTask) { return; }

      if (document.hidden) {
        stopPolling();
      } else if (!pollTimer) {
        pollTask();
        pollTimer = setInterval(pollTask, pollMs);
      }
    });
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

    /** Redraw whatever the application owns; called after every core change. */
    onRender: function (callback) { hooks.render = callback; },

    /** Where the application shows a short sentence: "Signed out.", and such. */
    onStatus: function (callback) { hooks.status = callback; }
  };
})();
