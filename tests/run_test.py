#!/usr/bin/env python
"""快速测试脚本 - 启动模拟器并运行基础测试"""

import subprocess
import time
import sys
import os
from pathlib import Path


def run_test():
    """运行测试"""
    print("=" * 60)
    print("PIP-Link 基础功能测试")
    print("=" * 60)

    # 获取项目根目录
    project_root = Path(__file__).parent.parent
    os.chdir(project_root)

    print("\n[1/3] 启动机载端服务器...")
    print("      运行: python air_unit_server.py --verbose")

    # 启动服务器
    simulator_process = subprocess.Popen(
        [sys.executable, "air_unit_server.py", "--verbose"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # 等待模拟器启动
    time.sleep(2)

    print("✓ 机载端服务器已启动")
    print("\n[2/3] 启动客户端...")
    print("      运行: python main.py")
    print("\n      客户端启动后：")
    print("      1. 按 ESC 打开菜单")
    print("      2. 点击 SCAN 按钮发现机载端")
    print("      3. 点击 Connect 连接")
    print("      4. 观察统计信息（FPS、RTT、丢包率）")
    print("      5. 按 ESC 关闭菜单，使用 WASD 控制")
    print("      6. 按 ESC 打开菜单，点击 Disconnect 断开")

    # 启动客户端
    try:
        client_process = subprocess.Popen(
            [sys.executable, "main.py"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        print("\n✓ 客户端已启动")
        print("\n[3/3] 等待测试完成...")

        # 等待客户端退出
        client_process.wait()

    except KeyboardInterrupt:
        print("\n\n用户中断测试")
        client_process.terminate()
    finally:
        # 等待模拟器完成
        simulator_process.wait()

        print("\n" + "=" * 60)
        print("测试完成")
        print("=" * 60)

        # 打印模拟器输出
        print("\n机载端服务器统计：")
        stdout, stderr = simulator_process.communicate()
        if stdout:
            print(stdout)
        if stderr:
            print("错误:", stderr)


if __name__ == "__main__":
    run_test()
