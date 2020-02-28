# -*- coding: UTF-8 -*-

import time
import win32api
import win32con
import win32gui

class HuataiClient:

    WIN_ENCODING = "gbk"
    PY_ENCODING = "utf-8"

    @classmethod
    def __ConvertEncoding(cls, string, py_to_win):
        if not string:
            return None
        if py_to_win:
            return string.decode(cls.PY_ENCODING).encode(cls.WIN_ENCODING)
        else:
            return string.decode(cls.WIN_ENCODING).encode(cls.PY_ENCODING)

    @classmethod
    def __WinExec(cls, cmdline, show):
        return win32api.WinExec(cls.__ConvertEncoding(cmdline, True), show)

    @classmethod
    def __FindWindow(cls, class_name, win_title):
        return win32gui.FindWindow(cls.__ConvertEncoding(class_name, True),
                cls.__ConvertEncoding(win_title, True))

    @classmethod
    def __FindWindowEx(cls, parent_hwnd, child_hwnd_after,
            class_name, win_title):
        return win32gui.FindWindowEx(parent_hwnd, child_hwnd_after,
                cls.__ConvertEncoding(class_name, True),
                cls.__ConvertEncoding(win_title, True))

    @classmethod
    def __IsWindowVisible(cls, hwnd):
        return win32gui.IsWindowVisible(hwnd)

    @classmethod
    def __EnumWindows(cls, callback, extra):
        win32gui.EnumWindows(callback, extra)

    @classmethod
    def __SendMessage(cls, hwnd, id_msg, w_param, l_param):
        return win32api.SendMessage(hwnd, id_msg, w_param, l_param)

    @classmethod
    def __ClickButton(cls, hwnd):
        cls.__SendMessage(hwnd, win32con.BM_CLICK, None, None)

    @classmethod
    def __CloseWindow(cls, hwnd):
        cls.__SendMessage(hwnd, win32con.WM_CLOSE, None, None)

    @classmethod
    def __SetText(cls, hwnd, text):
        cls.__SendMessage(hwnd, win32con.WM_SETTEXT, None,
                cls.__ConvertEncoding(text, True))

    @classmethod
    def __GetText(cls, hwnd):
        length = cls.__SendMessage(hwnd, win32con.WM_GETTEXTLENGTH,
                None, None) + 1
        buf = win32gui.PyMakeBuffer(length)
        cls.__SendMessage(hwnd, win32con.WM_GETTEXT, length, buf)
        address, length = win32gui.PyGetBufferAddressAndLen(buf)
        text = win32gui.PyGetString(address, length - 1)
        return cls.__ConvertEncoding(text, False)

    @classmethod
    def __Login(cls, exec_path, user_id, trade_passwd, comm_passwd,
            timeout):
        title = "用户登录"
        hwnd = cls.__FindWindow(None, title)
        if not hwnd:
            cls.__WinExec(exec_path, True)
            for i in range(timeout):
                time.sleep(1)
                hwnd = cls.__FindWindow(None, title)
                if hwnd:
                    break
            if not hwnd:
                raise Exception("Cannot open client")
        assert(cls.__IsWindowVisible(hwnd))
        user_id_box = cls.__FindWindowEx(hwnd, None, "ComboBox", None)
        assert(user_id_box)
        user_id_edit = cls.__FindWindowEx(user_id_box, None, "Edit", None)
        assert(user_id_edit)
        trade_passwd_edit = cls.__FindWindowEx(hwnd, user_id_box, "Edit", None)
        assert(trade_passwd_edit)
        comm_passwd_edit = cls.__FindWindowEx(hwnd, trade_passwd_edit,
                "Edit", None)
        assert(comm_passwd_edit)
        submit_button = cls.__FindWindowEx(hwnd, comm_passwd_edit, "Button",
                "确定(&Y)")
        assert(submit_button)
        cls.__SetText(user_id_edit, user_id)
        cls.__SetText(trade_passwd_edit, trade_passwd)
        cls.__SetText(comm_passwd_edit, comm_passwd)
        cls.__ClickButton(submit_button)

    @classmethod
    def __InitMainWindow(cls, exec_path, user_id, trade_passwd, comm_passwd,
            timeout):

        def get_main_window():
            class_name = "AfxFrameOrView42s"
            win_title = "网上股票交易系统5.0"
            hwnd = cls.__FindWindow(class_name, win_title)
            if hwnd and cls.__IsWindowVisible(hwnd):
                return hwnd
            return None

        hwnd = get_main_window()
        if not hwnd:
            cls.__Login(exec_path, user_id, trade_passwd, comm_passwd, timeout)
            for i in range(timeout):
                time.sleep(1)
                hwnd = get_main_window()
                if hwnd:
                    break
            if not hwnd:
                raise Exception("Cannot login client")

        def is_safe_info_window(hwnd):
            encrypt_box = cls.__FindWindowEx(hwnd, None, "ComboBox", None)
            if not (encrypt_box and cls.__GetText(encrypt_box) == "核新加密"):
                return False
            close_button = cls.__FindWindowEx(hwnd, encrypt_box, "Button",
                    None)
            if not close_button:
                return False
            title_static = cls.__FindWindowEx(hwnd, close_button, "Static",
                    None)
            if not (title_static and cls.__GetText(title_static) ==
                    "安全信息及设置"):
                return False
            return True

        def is_announcement_window(hwnd):
            submit_button = cls.__FindWindowEx(hwnd, None, "Button", "确定")
            if not submit_button:
                return False
            title_static = cls.__FindWindowEx(hwnd, submit_button, "Static", None)
            if not (title_static and cls.__GetText(title_static) == "营业部公告"):
                return False
            return True

        def close_sub_windows(hwnd, extra):
            if is_safe_info_window(hwnd) or is_announcement_window(hwnd):
                cls.__CloseWindow(hwnd)

        time.sleep(timeout)
        cls.__EnumWindows(close_sub_windows, None)
        return hwnd

    def __init__(self, exec_path, user_id, trade_passwd, comm_passwd,
            timeout = 30):
        cls = self.__class__
        self.__hwnd = cls.__InitMainWindow(exec_path, user_id, trade_passwd,
                comm_passwd, timeout)
        self.__timeout = timeout

    def Buy(self, symbol, price, amount):
        pass

h = HuataiClient("C:\\Program Files\\htwt\\xiadan.exe", "666622916299", "546712", "1234abcd", 10)