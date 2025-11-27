#!/bin/bash
# 代码更新脚本
# 用于同步 upstream 代码到 main 分支，然后合并到 dev 分支

set -e  # 遇到错误立即退出

# ========== 配置区域 ==========
# 上游仓库 URL（可根据需要修改）
UPSTREAM_REPO_URL="git@github.com:78/xiaozhi-esp32.git"

# 分支名称（可根据需要修改）
MAIN_BRANCH="main"
DEV_BRANCH="dev"
# ==============================

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否在 Git 仓库中
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "当前目录不是 Git 仓库！"
    exit 1
fi

# 检查并配置 upstream 远程仓库
print_info "检查 upstream 远程仓库配置..."
if ! git remote | grep -q "^upstream$"; then
    print_warning "未找到 upstream 远程仓库，正在配置..."
    git remote add upstream "$UPSTREAM_REPO_URL"
    print_success "已添加 upstream 远程仓库"
else
    # 检查 upstream URL 是否正确
    UPSTREAM_URL=$(git remote get-url upstream 2>/dev/null || echo "")
    if [ "$UPSTREAM_URL" != "$UPSTREAM_REPO_URL" ]; then
        print_warning "upstream URL 不正确，正在更新..."
        git remote set-url upstream "$UPSTREAM_REPO_URL"
        print_success "已更新 upstream URL"
    else
        print_success "upstream 远程仓库已正确配置"
    fi
fi

# 保存当前分支
CURRENT_BRANCH=$(git branch --show-current)
print_info "当前分支: $CURRENT_BRANCH"

# 1. 更新 main 分支
print_info "========== 开始更新 $MAIN_BRANCH 分支 =========="
print_info "切换到 $MAIN_BRANCH 分支..."
git checkout "$MAIN_BRANCH"

print_info "从 upstream 获取最新代码..."
git fetch upstream

# 检查是否有更新
print_info "========== 检查 upstream/$MAIN_BRANCH 的更新 =========="
COMMITS_LIST=$(git log HEAD..upstream/$MAIN_BRANCH --oneline 2>/dev/null || echo "")

if [ -n "$COMMITS_LIST" ]; then
    # 统计提交数量（兼容不同系统）
    if command -v wc >/dev/null 2>&1; then
        COMMITS_AHEAD=$(echo "$COMMITS_LIST" | wc -l)
    else
        COMMITS_AHEAD=$(echo "$COMMITS_LIST" | grep -c . || echo "0")
    fi
    
    print_warning "发现 $COMMITS_AHEAD 个新提交"
    
    # 显示提交列表
    echo ""
    print_info "新提交列表:"
    echo "$COMMITS_LIST"
    
    # 显示文件变化统计
    echo ""
    print_info "文件变化统计:"
    git diff --stat HEAD..upstream/$MAIN_BRANCH
    
    echo ""
    read -p "是否查看详细代码变化？(y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git diff HEAD..upstream/$MAIN_BRANCH | less
    fi
    
    echo ""
    print_info "合并 upstream/$MAIN_BRANCH 到本地 $MAIN_BRANCH 分支..."
    git merge upstream/$MAIN_BRANCH
    
    print_info "推送 $MAIN_BRANCH 分支到远程仓库..."
    git push origin "$MAIN_BRANCH"
    print_success "$MAIN_BRANCH 分支更新完成"
else
    print_success "$MAIN_BRANCH 分支已经是最新的，无需更新"
fi

# 2. 更新 dev 分支
print_info ""
print_info "========== 开始更新 $DEV_BRANCH 分支 =========="
print_info "切换到 $DEV_BRANCH 分支..."
git checkout "$DEV_BRANCH"

# 检查 dev 分支是否存在
if ! git show-ref --verify --quiet refs/heads/$DEV_BRANCH; then
    print_error "$DEV_BRANCH 分支不存在！"
    exit 1
fi

# 检查是否需要合并
COMMITS_TO_MERGE_LIST=$(git log $DEV_BRANCH..$MAIN_BRANCH --oneline 2>/dev/null || echo "")

if [ -n "$COMMITS_TO_MERGE_LIST" ]; then
    # 统计提交数量（兼容不同系统）
    if command -v wc >/dev/null 2>&1; then
        COMMITS_TO_MERGE=$(echo "$COMMITS_TO_MERGE_LIST" | wc -l)
    else
        COMMITS_TO_MERGE=$(echo "$COMMITS_TO_MERGE_LIST" | grep -c . || echo "0")
    fi
    
    print_warning "发现 $COMMITS_TO_MERGE 个提交需要合并到 $DEV_BRANCH"
    
    print_info "合并 $MAIN_BRANCH 分支到 $DEV_BRANCH 分支..."
    git merge "$MAIN_BRANCH"
    
    print_info "推送 $DEV_BRANCH 分支到远程仓库..."
    git push origin "$DEV_BRANCH"
    print_success "$DEV_BRANCH 分支更新完成"
else
    print_success "$DEV_BRANCH 分支已经包含 $MAIN_BRANCH 的所有更新，无需合并"
fi

# 恢复原分支（如果原分支不是 main 或 dev）
if [ "$CURRENT_BRANCH" != "$MAIN_BRANCH" ] && [ "$CURRENT_BRANCH" != "$DEV_BRANCH" ]; then
    print_info "切换回原分支: $CURRENT_BRANCH"
    git checkout "$CURRENT_BRANCH"
fi

print_success ""
print_success "========== 代码更新完成 =========="
print_info "$MAIN_BRANCH 分支: $(git log -1 --oneline refs/heads/$MAIN_BRANCH 2>/dev/null || echo '无法获取')"
print_info "$DEV_BRANCH 分支: $(git log -1 --oneline refs/heads/$DEV_BRANCH 2>/dev/null || echo '无法获取')"

